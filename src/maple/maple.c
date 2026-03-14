#include "maple.h"
#include "maple.pio.h"          // Generated from maple.pio by pico_generate_pio_header()
#include "config.h"
#include "controller.h"

#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include <string.h>
#include <stdio.h>

// ── Device Identity ───────────────────────────────────────────────────────────
// Returned in response to MAPLE_CMD_DEVICE_INFO.
// We advertise ourselves as a standard digital + analog controller.
//
// Layout (all fields little-endian):
//   func          4 bytes  — function type bitmask
//   func_data[0]  4 bytes  — controller capability bitmask (TODO: verify exact bits)
//   func_data[1]  4 bytes  — 0x00000000 (unused sub-function)
//   func_data[2]  4 bytes  — 0x00000000 (unused sub-function)
//   area_code     1 byte   — 0xFF = all regions
//   connector     1 byte   — 0x00 = standard connector
//   product_name 30 bytes  — null-padded ASCII
//   license      60 bytes  — null-padded ASCII
//   standby_mw   2 bytes   — standby power in mW
//   max_mw       2 bytes   — max power in mW
static const uint8_t k_device_info_payload[] = {
    // func: controller
    0x01, 0x00, 0x00, 0x00,
    // func_data[0]: supports all standard buttons + 4 analog axes
    // TODO: cross-reference with Maple spec for exact capability bitmask
    0xFE, 0x06, 0x7E, 0x00,
    // func_data[1], func_data[2]: unused
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    // area_code, connector_direction
    0xFF, 0x00,
    // product_name (30 bytes)
    'B','t','2','M','a','p','l','e',' ','C','o','n','t','r','o','l','l','e','r',
    0,0,0,0,0,0,0,0,0,0,0,
    // license (60 bytes)
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,
    // standby_mw, max_mw
    0x04, 0x00,
    0x04, 0x00,
};

// ── Buffers ───────────────────────────────────────────────────────────────────
// Max Maple frame: 255 payload words + 1 header word + 1 CRC byte ≈ 1025 bytes.
// Round up to a safe aligned size.
static uint32_t rx_buf[256];
static uint32_t tx_buf[256];
static uint32_t tx_words;  // Number of 32-bit words in tx_buf to send

// ── CRC ───────────────────────────────────────────────────────────────────────
// Maple CRC is a simple XOR of all bytes in the frame (header + payload).
static uint8_t maple_crc(const uint8_t *data, uint32_t len) {
    uint8_t crc = 0;
    for (uint32_t i = 0; i < len; i++) crc ^= data[i];
    return crc;
}

// ── Response Builders ─────────────────────────────────────────────────────────
// Each builder fills tx_buf and returns the total byte count (including CRC).

static uint32_t build_device_info(uint8_t dest, uint8_t src) {
    uint8_t *p = (uint8_t *)tx_buf;
    uint32_t payload_bytes = sizeof(k_device_info_payload);
    uint32_t payload_words = (payload_bytes + 3) / 4;

    p[0] = (uint8_t)payload_words;
    p[1] = MAPLE_RESP_DEVICE_INFO;
    p[2] = src;     // src/dest are swapped in the response
    p[3] = dest;
    memcpy(p + 4, k_device_info_payload, payload_bytes);

    // Pad to word boundary
    uint32_t padded = 4 + payload_words * 4;
    for (uint32_t i = 4 + payload_bytes; i < padded; i++) p[i] = 0;

    p[padded] = maple_crc(p, padded);
    return padded + 1;
}

static uint32_t build_condition(uint8_t dest, uint8_t src) {
    dc_controller_state_t state;
    controller_state_read(&state);

    uint8_t *p = (uint8_t *)tx_buf;
    // Payload: func word (4 bytes) + dc_controller_state_t (8 bytes) = 12 bytes = 3 words
    p[0] = 3;
    p[1] = MAPLE_RESP_DATA;
    p[2] = src;
    p[3] = dest;
    // Function type word
    p[4] = 0x01; p[5] = 0x00; p[6] = 0x00; p[7] = 0x00;
    // Controller state (8 bytes, matches dc_controller_state_t packed layout)
    memcpy(p + 8, &state, sizeof(state));

    uint32_t total = 4 + 3 * 4;    // header + 3 payload words
    p[total] = maple_crc(p, total);
    return total + 1;
}

// ── Command Dispatch ──────────────────────────────────────────────────────────
static void handle_command(uint32_t rx_word_count) {
    maple_header_t *hdr = (maple_header_t *)rx_buf;
    uint32_t byte_count = 0;

    switch (hdr->command) {
        case MAPLE_CMD_DEVICE_INFO:
        case MAPLE_CMD_ALL_DEVICE_INFO:
            byte_count = build_device_info(hdr->destination, hdr->source);
            break;

        case MAPLE_CMD_GET_CONDITION:
            byte_count = build_condition(hdr->destination, hdr->source);
            break;

        case MAPLE_CMD_RESET:
        case MAPLE_CMD_SHUTDOWN:
            byte_count = 0;     // No response required
            break;

        default:
            printf("[maple] Unknown cmd 0x%02X\n", hdr->command);
            byte_count = 0;
            break;
    }

    // Round byte_count up to whole 32-bit words for the TX FIFO
    tx_words = (byte_count + 3) / 4;
}

// ── PIO Setup ─────────────────────────────────────────────────────────────────
void maple_init(void) {
    // TODO: Load RX and TX PIO programs once maple.pio is complete.
    //
    // Outline:
    //   uint rx_offset = pio_add_program(MAPLE_PIO, &maple_rx_program);
    //   uint tx_offset = pio_add_program(MAPLE_PIO, &maple_tx_program);
    //
    //   // Both SMs share the same two GPIO pins.
    //   // RX SM: set pins as inputs (no output enable).
    //   // TX SM: set pins as outputs only during transmission,
    //   //        then relinquish to RX SM.
    //
    //   // Clock divider: Maple is ~2 Mbps → 500 ns per half-bit.
    //   // At 150 MHz: 75 cycles per half-bit.
    //   float clk_div = (float)clock_get_hz(clk_sys) / (2.0f * 1e6f * MAPLE_PIO_CYCLES_PER_BIT);
    //   pio_sm_set_clkdiv(MAPLE_PIO, MAPLE_SM_RX, clk_div);
    //   pio_sm_set_clkdiv(MAPLE_PIO, MAPLE_SM_TX, clk_div);
    //
    //   pio_sm_init(MAPLE_PIO, MAPLE_SM_RX, rx_offset, &rx_config);
    //   pio_sm_init(MAPLE_PIO, MAPLE_SM_TX, tx_offset, &tx_config);
    //
    //   pio_sm_set_enabled(MAPLE_PIO, MAPLE_SM_RX, true);
    //   // TX SM starts disabled; enabled only when sending a response.

    printf("[maple] init (PIO stub — not yet implemented)\n");
}

// ── Main Loop ─────────────────────────────────────────────────────────────────
// Runs on Core 0.  No interrupts.  No sleep.  No other work.
void __attribute__((noreturn)) maple_run(void) {
    printf("[maple] run loop started\n");

    while (true) {
        // TODO: Replace with real PIO RX FIFO polling once maple.pio is done.
        //
        // Critical path (must complete within ~200 µs of frame end):
        //
        //   1. Poll RX FIFO — skip if empty (branch-prediction-friendly tight loop)
        //      if (pio_sm_is_rx_fifo_empty(MAPLE_PIO, MAPLE_SM_RX)) continue;
        //
        //   2. Read header word
        //      rx_buf[0] = pio_sm_get(MAPLE_PIO, MAPLE_SM_RX);
        //      maple_header_t *hdr = (maple_header_t *)rx_buf;
        //
        //   3. Read remaining payload words (blocking — they arrive quickly)
        //      for (uint32_t i = 1; i <= hdr->frame_words + 1 /*+CRC word*/; i++)
        //          rx_buf[i] = pio_sm_get_blocking(MAPLE_PIO, MAPLE_SM_RX);
        //
        //   4. Validate CRC
        //      (optional: skip in release for speed)
        //
        //   5. Dispatch
        //      handle_command(hdr->frame_words + 1);
        //
        //   6. Transmit response
        //      if (tx_words > 0) {
        //          pio_sm_set_enabled(MAPLE_PIO, MAPLE_SM_RX, false);
        //          pio_sm_set_enabled(MAPLE_PIO, MAPLE_SM_TX, true);
        //          for (uint32_t i = 0; i < tx_words; i++)
        //              pio_sm_put_blocking(MAPLE_PIO, MAPLE_SM_TX, tx_buf[i]);
        //          // Wait for TX SM to drain, then re-enable RX SM
        //          pio_sm_set_enabled(MAPLE_PIO, MAPLE_SM_TX, false);
        //          pio_sm_set_enabled(MAPLE_PIO, MAPLE_SM_RX, true);
        //      }
    }
}
