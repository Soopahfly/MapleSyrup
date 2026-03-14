#include "hid_map.h"
#include <string.h>
#include <stdio.h>

// ── DS4 (DualShock 4) ─────────────────────────────────────────────────────────
// BT Classic report ID 0x11.  Payload starts at byte offset 3 within the
// L2CAP/HID packet that BTstack delivers.
//
// Offsets below are relative to the start of the HID report payload
// (i.e. after stripping the 2-byte L2CAP header and the 1-byte report ID):
//
//   0  : left  stick X
//   1  : left  stick Y
//   2  : right stick X
//   3  : right stick Y
//   4  : dpad nibble (bits 3:0) + buttons (bits 7:4: square/cross/circle/tri)
//   5  : L1, R1, L2, R2, Share, Options, L3, R3
//   6  : PS button, touchpad click
//   7  : L2 axis
//   8  : R2 axis
//
// TODO: verify offsets against a real DS4 capture.

#define DS4_REPORT_ID   0x11

static void map_ds4(const uint8_t *data, uint16_t len,
                    dc_controller_state_t *state) {
    if (len < 10) return;

    // Start from neutral (all released)
    state->buttons       = DC_BUTTONS_RELEASED;
    state->right_trigger = DC_TRIGGER_RELEASED;
    state->left_trigger  = DC_TRIGGER_RELEASED;

    // Sticks: DS4 range 0-255, centre ~128. Same as DC — direct copy.
    state->joy_x  = data[0];
    state->joy_y  = data[1];
    state->joy2_x = data[2];
    state->joy2_y = data[3];

    // DPad — lower nibble of byte 4
    uint8_t dpad = data[4] & 0x0F;
    // DS4 dpad values: 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW, 8=none
    if (dpad == 0 || dpad == 1 || dpad == 7) state->buttons &= ~DC_BTN_DPAD_UP;
    if (dpad == 3 || dpad == 4 || dpad == 5) state->buttons &= ~DC_BTN_DPAD_DOWN;
    if (dpad == 5 || dpad == 6 || dpad == 7) state->buttons &= ~DC_BTN_DPAD_LEFT;
    if (dpad == 1 || dpad == 2 || dpad == 3) state->buttons &= ~DC_BTN_DPAD_RIGHT;

    // Face buttons (upper nibble of byte 4)
    // DS4: bit4=square, bit5=cross, bit6=circle, bit7=triangle
    // DC mapping: square→X, cross→A, circle→B, triangle→Y
    if (data[4] & (1 << 4)) state->buttons &= ~DC_BTN_X;    // square  → X
    if (data[4] & (1 << 5)) state->buttons &= ~DC_BTN_A;    // cross   → A
    if (data[4] & (1 << 6)) state->buttons &= ~DC_BTN_B;    // circle  → B
    if (data[4] & (1 << 7)) state->buttons &= ~DC_BTN_Y;    // triangle→ Y

    // Shoulder + bumpers (byte 5)
    // DS4: bit0=L1, bit1=R1, bit2=L2, bit3=R2
    // DC: L1→Z (closest analog), R1→C, Start=start
    if (data[5] & (1 << 0)) state->buttons &= ~DC_BTN_Z;    // L1 → Z
    if (data[5] & (1 << 1)) state->buttons &= ~DC_BTN_C;    // R1 → C
    if (data[5] & (1 << 5)) state->buttons &= ~DC_BTN_START; // Options → Start

    // Analog triggers (bytes 7 and 8)
    state->left_trigger  = data[7];
    state->right_trigger = data[8];
}

// ── DS5 (DualSense PS5) ───────────────────────────────────────────────────────
// BT Classic report ID 0x31.  Payload layout is similar to DS4 but shifted.
// TODO: capture and verify offsets with a real DualSense.
#define DS5_REPORT_ID   0x31

static void map_ds5(const uint8_t *data, uint16_t len,
                    dc_controller_state_t *state) {
    // Placeholder — DS5 layout is close enough to DS4 that the same mapping
    // works for initial testing.  Verify byte offsets before shipping.
    map_ds4(data, len, state);
}

// ── Generic / fallback ────────────────────────────────────────────────────────
// Used for unknown controllers.  Outputs a neutral (no-input) state.
static void map_generic(const uint8_t *data, uint16_t len,
                        dc_controller_state_t *state) {
    (void)data; (void)len;
    state->buttons       = DC_BUTTONS_RELEASED;
    state->right_trigger = DC_TRIGGER_RELEASED;
    state->left_trigger  = DC_TRIGGER_RELEASED;
    state->joy_x         = DC_STICK_CENTER;
    state->joy_y         = DC_STICK_CENTER;
    state->joy2_x        = DC_STICK_CENTER;
    state->joy2_y        = DC_STICK_CENTER;
}

// ── Public Entry Point ────────────────────────────────────────────────────────
void hid_map_report(const uint8_t *report, uint16_t len,
                    dc_controller_state_t *state) {
    if (len < 2) return;

    uint8_t report_id = report[0];
    const uint8_t *data = report + 1;   // skip report ID byte
    uint16_t data_len   = len - 1;

    switch (report_id) {
        case DS4_REPORT_ID:
            map_ds4(data, data_len, state);
            break;
        case DS5_REPORT_ID:
            map_ds5(data, data_len, state);
            break;
        default:
            printf("[hid_map] Unknown report ID 0x%02X\n", report_id);
            map_generic(data, data_len, state);
            break;
    }
}
