#include "maple.h"
#include "maple.pio.h"
#include "vmu.h"
#include "config.h"
#include "controller.h"
#include "usb/usb_host.h"
#include "display/oled.h"

#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include <string.h>
#include <stdio.h>

// ── PIO clock dividers ────────────────────────────────────────────────────────
// TX: controls bit timing.  The dreamwave TX PIO uses ~4 PIO cycles per half-bit.
//   At 150 MHz sys-clock, 8.904 gives ~2 Mbps bit rate.
#define MAPLE_TX_CLOCK_DIVIDER 8.904f
// RX: run as fast as possible so we catch every edge.
#define MAPLE_RX_CLOCK_DIVIDER 1.0f

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
// raw_resp: raw byte buffer for building responses.  Header (4) + payload + CRC.
// tx_bytes: flat byte array pushed to PIO TX one byte at a time.
// rx_bytes: raw bytes received from PIO RX.
#define MAX_PACKET_BYTES  (4 + 256*4 + 1)
static uint8_t  raw_resp[MAX_PACKET_BYTES];
static uint32_t raw_resp_len;

#define MAX_RX_BYTES 512
static uint8_t rx_bytes[MAX_RX_BYTES];

static uint32_t rx_offset;  // PIO program offset for RX SM (needed to restart)

// ── CRC ───────────────────────────────────────────────────────────────────────
static uint8_t maple_crc(const uint8_t *data, uint32_t len) {
    uint8_t crc = 0;
    for (uint32_t i = 0; i < len; i++) crc ^= data[i];
    return crc;
}

// ── Response Builder Helpers ──────────────────────────────────────────────────
// Builds a complete Maple response frame into out[].
// Returns total byte count (header + padded payload + CRC).
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

// ── Controller Response Builders ──────────────────────────────────────────────
static uint32_t build_ctrl_device_info(uint8_t dest, uint8_t src) {
    return write_resp(raw_resp, MAPLE_RESP_DEVICE_INFO, dest, src,
                      k_ctrl_device_info, sizeof(k_ctrl_device_info));
}

static uint32_t build_ctrl_condition(uint8_t dest, uint8_t src) {
    dc_controller_state_t state;
    controller_state_read(&state);

    // Payload: function type word + 8 controller state bytes
    uint8_t payload[4 + sizeof(state)];
    // Function type: controller = 0x00000001 (little-endian)
    payload[0] = 0x01; payload[1] = 0x00; payload[2] = 0x00; payload[3] = 0x00;
    memcpy(payload + 4, &state, sizeof(state));
    return write_resp(raw_resp, MAPLE_RESP_DATA, dest, src,
                      payload, sizeof(payload));
}

static uint32_t build_ctrl_rumble_ack(uint8_t dest, uint8_t src,
                                       const uint8_t *payload_bytes, uint32_t payload_len) {
    // Word 0 of payload: func type (0x00000100 = vibration)
    // Word 1: intensity encoding
    if (payload_len >= 8) {
        uint32_t func_word;
        memcpy(&func_word, payload_bytes, 4);
        if (func_word & MAPLE_FUNC_VIBRATION) {
            uint8_t intensity = payload_bytes[5];   // typical rumble intensity byte
            controller_set_rumble(intensity);
        }
    }
    return write_resp(raw_resp, MAPLE_RESP_ACK, dest, src, NULL, 0);
}

// ── RX: receive a complete Maple frame ───────────────────────────────────────
// The maple_rx PIO handles SOP detection internally (see maple.pio start:).
// After SOP, it outputs one decoded Maple byte per FIFO push (8 decoded bits,
// left-shift autopush).  We just read bytes directly — no reconstruction needed.
//
// Frame layout from PIO:
//   byte 0 : frame_words  (number of 32-bit payload words)
//   byte 1 : command
//   byte 2 : destination address
//   byte 3 : source address
//   bytes 4..(4 + frame_words*4 - 1) : payload
//   last byte : CRC (XOR of all preceding bytes)
//
// Returns total byte count including CRC (≥5 on success, 0 on error/timeout).
static uint32_t rx_receive_frame(void) {
    PIO pio = MAPLE_PIO;
    uint sm = MAPLE_SM_RX;

    // Restart SM — it will run start: which waits for idle then brackets the
    // SOP with wait-for-SDCKB-LOW / wait-for-SDCKB-HIGH, clears the ISR via
    // push noblock, then enters the byte-decode loop.
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_exec(pio, sm, pio_encode_jmp(rx_offset));
    pio_sm_set_enabled(pio, sm, true);

    // Ensure both Maple pins are inputs for RX
    gpio_set_dir(PIN_MAPLE_SDCKA, GPIO_IN);
    gpio_set_dir(PIN_MAPLE_SDCKB, GPIO_IN);
    gpio_set_function(PIN_MAPLE_SDCKA, GPIO_FUNC_PIO0);
    gpio_set_function(PIN_MAPLE_SDCKB, GPIO_FUNC_PIO0);

    uint32_t bytes_read = 0;
    uint32_t max_bytes  = 5;   // 4 header + 1 CRC minimum
    uint8_t  checksum   = 0;

    // Initial timeout covers ≥2 DC polling intervals (~8 ms each) while the
    // SM waits for the SOP.  Between bytes the timeout is much shorter.
    uint32_t timeout = 20000000u;   // ~20 ms at 150 MHz

    while (timeout--) {
        if (pio_sm_is_rx_fifo_empty(pio, sm)) continue;

        // Each FIFO word contains one decoded Maple byte in bits [7:0].
        uint8_t packet_byte = (uint8_t)(pio_sm_get(pio, sm) & 0xFF);

        if (bytes_read < MAX_RX_BYTES)
            rx_bytes[bytes_read] = packet_byte;
        bytes_read++;

        if (bytes_read == 1) {
            // frame_words tells us how many 4-byte payload words follow
            max_bytes = 4 + (uint32_t)packet_byte * 4 + 1;
        } else if (bytes_read >= max_bytes) {
            // This is the CRC byte — verify
            if (checksum != packet_byte) {
                printf("[maple] RX CRC mismatch (got 0x%02X, want 0x%02X)\n",
                       packet_byte, checksum);
                return 0;
            }
            return bytes_read;
        }

        checksum ^= packet_byte;
        // Reset timeout between bytes — once a frame starts bytes come fast
        timeout = 1000000u;   // ~1 ms per byte is very generous at 2 Mbps
    }

    return 0;  // timeout
}

// ── Command Dispatch ──────────────────────────────────────────────────────────
// rx_bytes layout after rx_receive_frame():
//   [0] = frame_words  (payload word count, not counting header)
//   [1] = command
//   [2] = destination address
//   [3] = source address
//   [4..] = payload bytes
//   [last] = CRC (already verified)
static void handle_command(void) {
    if (raw_resp_len > 0) return;  // shouldn't happen

    uint8_t frame_words = rx_bytes[0];
    uint8_t command     = rx_bytes[1];
    uint8_t dest        = rx_bytes[2];
    uint8_t src         = rx_bytes[3];
    const uint8_t *payload = rx_bytes + 4;
    uint32_t payload_len   = (uint32_t)frame_words * 4;

    // Filter: only respond to our own addresses.
    // MAPLE_ADDR_CONTROLLER = 0x21 (Port A main peripheral, sub1 present).
    // MAPLE_ADDR_VMU        = 0x01 (Port A sub-peripheral slot 1).
    // Broadcast address     = 0x00 (all peripherals respond).
    // Sub-peripheral bit: dest & 0x01 non-zero means addressed to a sub-device.
    bool is_broadcast   = (dest == MAPLE_ADDR_HOST);
    // Accept 0x20 (no sub-peripherals) and 0x21 (sub-slot 1 present) — DC may
    // use either depending on whether it has seen our device info yet.
    bool is_controller  = (dest == 0x20) || (dest == 0x21);
    bool is_vmu         = (dest == MAPLE_ADDR_VMU) || (!is_controller && (dest & 0x01));
    if (!is_broadcast && !is_controller && !is_vmu) {
        return;
    }

    // Route sub-peripheral commands to VMU handler
    if (is_vmu) {
        raw_resp_len = vmu_handle_command(command, dest, src,
                                          (const uint32_t *)payload,
                                          frame_words, raw_resp);
        if (command == MAPLE_CMD_GAME_ID) {
            vmu_on_game_id(payload, (uint8_t)(payload_len));
        }
        return;
    }

    // Controller commands
    switch (command) {
        case MAPLE_CMD_DEVICE_INFO:
        case MAPLE_CMD_ALL_DEVICE_INFO:
            raw_resp_len = build_ctrl_device_info(dest, src);
            break;
        case MAPLE_CMD_GET_CONDITION:
            raw_resp_len = build_ctrl_condition(dest, src);
            break;
        case MAPLE_CMD_SET_CONDITION:
            raw_resp_len = build_ctrl_rumble_ack(dest, src, payload, payload_len);
            break;
        case MAPLE_CMD_RESET:
        case MAPLE_CMD_SHUTDOWN:
            raw_resp_len = 0;
            break;
        default:
            printf("[maple] unknown cmd 0x%02X to 0x%02X\n", command, dest);
            raw_resp_len = 0;
            break;
    }
}

// ── TX: transmit a response ───────────────────────────────────────────────────
// The dreamwave TX PIO handles SOP, data bit encoding (phase1/phase2), and EOP.
// We just push the raw response bytes to the TX FIFO (each byte as MSB of 32-bit word).
static void transmit_response(void) {
    PIO pio = MAPLE_PIO;
    uint sm = MAPLE_SM_TX;

    // Set pins as outputs and hand to TX SM
    pio_sm_set_consecutive_pindirs(pio, sm, PIN_MAPLE_SDCKA, 2, true);
    gpio_set_function(PIN_MAPLE_SDCKA,     GPIO_FUNC_PIO0);
    gpio_set_function(PIN_MAPLE_SDCKB,     GPIO_FUNC_PIO0);
    pio_sm_set_enabled(pio, sm, true);

    // Push each byte as upper byte of 32-bit word (PIO shifts out from MSB)
    for (uint32_t i = 0; i < raw_resp_len; i++) {
        pio_sm_put_blocking(pio, sm, (uint32_t)raw_resp[i] << 24);
    }

    // Wait for TX FIFO to drain
    while (!pio_sm_is_tx_fifo_empty(pio, sm))
        tight_loop_contents();

    // Wait for PIO to stall (all data shifted out)
    pio0->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
    while (!(pio0->fdebug & (1u << (PIO_FDEBUG_TXSTALL_LSB + sm))))
        tight_loop_contents();

    pio_sm_set_enabled(pio, sm, false);

    // Return pins to input for RX SM
    pio_sm_set_consecutive_pindirs(pio, sm, PIN_MAPLE_SDCKA, 2, false);
    gpio_set_function(PIN_MAPLE_SDCKA, GPIO_FUNC_PIO0);
    gpio_set_function(PIN_MAPLE_SDCKB, GPIO_FUNC_PIO0);
}

// ── Init ──────────────────────────────────────────────────────────────────────
void maple_init(void) {
    uint tx_offset = pio_add_program(MAPLE_PIO, &maple_tx_program);
    rx_offset      = pio_add_program(MAPLE_PIO, &maple_rx_program);

    maple_tx_program_init(MAPLE_PIO, MAPLE_SM_TX, tx_offset,
                          PIN_MAPLE_SDCKA, MAPLE_TX_CLOCK_DIVIDER);
    maple_rx_program_init(MAPLE_PIO, MAPLE_SM_RX, rx_offset,
                          PIN_MAPLE_SDCKA, MAPLE_RX_CLOCK_DIVIDER);

    // TX SM starts disabled; RX SM starts enabled watching for SOF.
    pio_sm_set_enabled(MAPLE_PIO, MAPLE_SM_TX, false);
    pio_sm_set_enabled(MAPLE_PIO, MAPLE_SM_RX, true);

    vmu_init();

    printf("[maple] init: SDCKA=GP%u SDCKB=GP%u TX_DIV=%.3f RX_DIV=%.3f\n",
           PIN_MAPLE_SDCKA, PIN_MAPLE_SDCKB,
           (double)MAPLE_TX_CLOCK_DIVIDER, (double)MAPLE_RX_CLOCK_DIVIDER);
}

// ── Main Loop ─────────────────────────────────────────────────────────────────
// Runs on Core 0.  Waits for Maple frames, decodes them, builds and sends responses.
void __attribute__((noreturn)) __time_critical_func(maple_run)(void) {
    printf("[maple] run loop started\n");

    while (true) {
        // ── 0. Service USB host events ────────────────────────────────────────
        // tuh_task() is non-blocking; it processes any pending TinyUSB events
        // and returns immediately if there is nothing to do.  Calling it here
        // ensures wired controllers are serviced once per Maple frame (~5 ms).
        usb_host_task();

        // ── 1. Receive a complete Maple frame ─────────────────────────────────
        uint32_t rx_len = rx_receive_frame();
        if (rx_len < 4) continue;   // timeout or framing error

        // ── 2. Build response ─────────────────────────────────────────────────
        raw_resp_len = 0;
        handle_command();

        // ── 3. Transmit response ──────────────────────────────────────────────
        if (raw_resp_len > 0)
            transmit_response();

        // ── 4. Update OLED if VMU LCD was written ─────────────────────────────
        // vmu_lcd_dirty() clears the flag on read — only true after a SET_CONDITION
        // LCD write, so this path is rarely taken and I2C cost is acceptable.
        if (vmu_lcd_dirty())
            oled_show_vmu(vmu_get_lcd());
    }
}
