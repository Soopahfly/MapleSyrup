#include "maple.h"
#include "maple.pio.h"
#include "vmu.h"
#include "config.h"
#include "controller.h"

#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include <string.h>
#include <stdio.h>

// ── Controller Device Info Payload ────────────────────────────────────────────
// func: controller (0x00000001)
// func_data[0]: capabilities bitmask
//   Bits 8-15 (buttons): C,B,A,Start,Up,Down,Left,Right,Z,Y,X,D = 0x0FFF
//   Bits 16-23 (analog channels): right trigger, left trigger, joy X, joy Y,
//                                  joy2 X, joy2 Y = 6 channels
static const uint8_t k_ctrl_device_info[] = {
    0x01, 0x00, 0x00, 0x00,         // func: controller
    0xFE, 0x06, 0x7E, 0x00,         // func_data[0]: buttons + 4 analog axes
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00,                     // area_code=all, connector=standard
    // product_name (30 bytes)
    'M','a','p','l','e','S','y','r','u','p',' ','C','o','n','t','r','o','l','l','e','r',
    0,0,0,0,0,0,0,0,0,
    // license (60 bytes)
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,
    0x04, 0x00,                     // standby_mw
    0x04, 0x00,                     // max_mw
};

// ── Buffers ───────────────────────────────────────────────────────────────────
// raw_resp: intermediate byte buffer for building unencoded responses.
// tx_buf:   pre-encoded 32-bit words pushed to the PIO TX FIFO.
static uint8_t  raw_resp[4 + 256*4 + 1];   // max frame
static uint32_t tx_buf[2 + 256 + 2];       // SOF(2) + encoded bytes + EOF(1)
static uint32_t tx_words;

static uint32_t rx_buf[256];
static uint32_t rx_offset;  // PIO program offset for RX SM (needed to restart)

// ── CRC ───────────────────────────────────────────────────────────────────────
static uint8_t maple_crc(const uint8_t *data, uint32_t len) {
    uint8_t crc = 0;
    for (uint32_t i = 0; i < len; i++) crc ^= data[i];
    return crc;
}

// ── Response Builder Helpers ──────────────────────────────────────────────────
static uint32_t write_resp(uint8_t *out, uint8_t resp_code,
                            uint8_t dest, uint8_t src,
                            const uint8_t *payload, uint32_t payload_bytes) {
    uint32_t payload_words = (payload_bytes + 3) / 4;
    out[0] = (uint8_t)payload_words;
    out[1] = resp_code;
    out[2] = src;
    out[3] = dest;
    if (payload && payload_bytes) {
        memcpy(out + 4, payload, payload_bytes);
        uint32_t padded = payload_words * 4;
        for (uint32_t i = payload_bytes; i < padded; i++) out[4 + i] = 0;
    }
    uint32_t total = 4 + payload_words * 4;
    out[total] = maple_crc(out, total);
    return total + 1;
}

// ── TX Encoding ───────────────────────────────────────────────────────────────
// Converts a raw response byte buffer into PIO-ready TX FIFO words.
// Format: SOF (2 words) + one 32-bit word per byte + EOF (1 word).
static uint32_t encode_tx(const uint8_t *bytes, uint32_t len) {
    uint32_t w = 0;
    maple_encode_sof(&tx_buf[0]);
    w = 2;
    for (uint32_t i = 0; i < len; i++)
        tx_buf[w++] = maple_encode_byte(bytes[i]);
    tx_buf[w++] = maple_encode_eof();
    return w;
}

// ── Controller Response Builders ──────────────────────────────────────────────
static uint32_t build_ctrl_device_info(uint8_t dest, uint8_t src) {
    return write_resp(raw_resp, MAPLE_RESP_DEVICE_INFO, dest, src,
                      k_ctrl_device_info, sizeof(k_ctrl_device_info));
}

static uint32_t build_ctrl_condition(uint8_t dest, uint8_t src) {
    dc_controller_state_t state;
    controller_state_read(&state);

    uint8_t payload[4 + sizeof(state)];
    // Function type word
    payload[0] = 0x01; payload[1] = 0x00; payload[2] = 0x00; payload[3] = 0x00;
    memcpy(payload + 4, &state, sizeof(state));
    return write_resp(raw_resp, MAPLE_RESP_DATA, dest, src,
                      payload, sizeof(payload));
}

static uint32_t build_ctrl_rumble_ack(uint8_t dest, uint8_t src,
                                       const uint32_t *payload_words, uint8_t nwords) {
    // Extract rumble intensity from SET_CONDITION payload.
    // Word 0: func type (0x00000100 = vibration)
    // Word 1: [intensity_left << 8 | intensity_right] (typical encoding)
    if (nwords >= 2 && (payload_words[0] & MAPLE_FUNC_VIBRATION)) {
        uint8_t intensity = (payload_words[1] >> 8) & 0xFF;
        controller_set_rumble(intensity);
    }
    return write_resp(raw_resp, MAPLE_RESP_ACK, dest, src, NULL, 0);
}

// ── Command Dispatch ──────────────────────────────────────────────────────────
static void handle_command(const maple_header_t *hdr) {
    uint32_t raw_bytes = 0;
    const uint32_t *payload = &rx_buf[1]; // payload words start after header

    if (hdr->destination == MAPLE_ADDR_VMU) {
        // Route all VMU commands to vmu.c
        raw_bytes = vmu_handle_command(hdr->command, hdr->destination, hdr->source,
                                       payload, hdr->frame_words, raw_resp);
        if (hdr->command == MAPLE_CMD_GAME_ID) {
            vmu_on_game_id((const uint8_t *)payload, hdr->frame_words * 4);
        }

    } else {
        // Controller commands (addressed to MAPLE_ADDR_CONTROLLER or 0x20)
        switch (hdr->command) {
            case MAPLE_CMD_DEVICE_INFO:
            case MAPLE_CMD_ALL_DEVICE_INFO:
                raw_bytes = build_ctrl_device_info(hdr->destination, hdr->source);
                break;
            case MAPLE_CMD_GET_CONDITION:
                raw_bytes = build_ctrl_condition(hdr->destination, hdr->source);
                break;
            case MAPLE_CMD_SET_CONDITION:
                raw_bytes = build_ctrl_rumble_ack(hdr->destination, hdr->source,
                                                   payload, hdr->frame_words);
                break;
            case MAPLE_CMD_RESET:
            case MAPLE_CMD_SHUTDOWN:
                break;
            default:
                printf("[maple] unknown cmd 0x%02X to 0x%02X\n",
                       hdr->command, hdr->destination);
                break;
        }
    }

    tx_words = (raw_bytes > 0) ? encode_tx(raw_resp, raw_bytes) : 0;
}

// ── PIO Helpers ───────────────────────────────────────────────────────────────
static void rx_sm_restart(void) {
    pio_sm_set_enabled(MAPLE_PIO, MAPLE_SM_RX, false);
    pio_sm_clear_fifos(MAPLE_PIO, MAPLE_SM_RX);
    pio_sm_restart(MAPLE_PIO, MAPLE_SM_RX);
    pio_sm_exec(MAPLE_PIO, MAPLE_SM_RX, pio_encode_jmp(rx_offset));
    pio_sm_set_enabled(MAPLE_PIO, MAPLE_SM_RX, true);
}

static void transmit_response(void) {
    // Hand ownership of the bus pins to the TX SM.
    pio_sm_set_consecutive_pindirs(MAPLE_PIO, MAPLE_SM_TX,
                                   PIN_MAPLE_SDCKA, 2, true);
    pio_sm_set_enabled(MAPLE_PIO, MAPLE_SM_TX, true);

    for (uint32_t i = 0; i < tx_words; i++)
        pio_sm_put_blocking(MAPLE_PIO, MAPLE_SM_TX, tx_buf[i]);

    // Wait for the TX FIFO and shift register to drain.
    while (!pio_sm_is_tx_fifo_empty(MAPLE_PIO, MAPLE_SM_TX))
        tight_loop_contents();
    // One extra bit-period to let the last word fully clock out.
    busy_wait_us(2);

    pio_sm_set_enabled(MAPLE_PIO, MAPLE_SM_TX, false);
    // Return pins to input for the RX SM.
    pio_sm_set_consecutive_pindirs(MAPLE_PIO, MAPLE_SM_RX,
                                   PIN_MAPLE_SDCKA, 2, false);
}

// ── Init ──────────────────────────────────────────────────────────────────────
void maple_init(void) {
    uint tx_offset = pio_add_program(MAPLE_PIO, &maple_tx_program);
    rx_offset      = pio_add_program(MAPLE_PIO, &maple_rx_program);

    maple_tx_program_init(MAPLE_PIO, MAPLE_SM_TX, tx_offset, PIN_MAPLE_SDCKA);
    maple_rx_program_init(MAPLE_PIO, MAPLE_SM_RX, rx_offset, PIN_MAPLE_SDCKA);

    // TX SM starts disabled; RX SM starts enabled watching for SOF.
    pio_sm_set_enabled(MAPLE_PIO, MAPLE_SM_TX, false);
    pio_sm_set_enabled(MAPLE_PIO, MAPLE_SM_RX, true);

    vmu_init();

    printf("[maple] init: SDCKA=GP%u SDCKB=GP%u\n",
           PIN_MAPLE_SDCKA, PIN_MAPLE_SDCKB);
}

// ── Main Loop ─────────────────────────────────────────────────────────────────
// Runs on Core 0.  Tight-polls the PIO RX FIFO — no interrupts, no sleep.
void __attribute__((noreturn)) __time_critical_func(maple_run)(void) {
    printf("[maple] run loop started\n");

    while (true) {
        // ── 1. Wait for first word (header) ───────────────────────────────────
        if (pio_sm_is_rx_fifo_empty(MAPLE_PIO, MAPLE_SM_RX)) continue;

        rx_buf[0] = pio_sm_get(MAPLE_PIO, MAPLE_SM_RX);
        maple_header_t *hdr = (maple_header_t *)&rx_buf[0];

        // ── 2. Read payload words ─────────────────────────────────────────────
        for (uint8_t i = 1; i <= hdr->frame_words; i++)
            rx_buf[i] = pio_sm_get_blocking(MAPLE_PIO, MAPLE_SM_RX);

        // ── 3. Re-arm RX SM immediately so we don't miss the next SOF ─────────
        rx_sm_restart();

        // ── 4. Filter: only respond to our own addresses ──────────────────────
        uint8_t dst = hdr->destination;
        if (dst != MAPLE_ADDR_CONTROLLER && dst != MAPLE_ADDR_VMU &&
            dst != (MAPLE_ADDR_CONTROLLER & 0xC0)) {   // also accept 0x20 bare
            continue;
        }

        // ── 5. Build response ─────────────────────────────────────────────────
        handle_command(hdr);

        // ── 6. Transmit ───────────────────────────────────────────────────────
        if (tx_words > 0)
            transmit_response();
    }
}
