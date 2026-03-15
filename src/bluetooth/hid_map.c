// hid_map.c — Map Bluepad32 normalised gamepad → Dreamcast controller state
//
// Bluepad32 presents every controller through a single uni_gamepad_t struct:
//   axis_x/y        left stick   -512..+511
//   axis_rx/ry      right stick  -512..+511
//   brake           L2/LT        0..1023
//   throttle        R2/RT        0..1023
//   buttons         BUTTON_A/B/X/Y/SHOULDER_L/R bitmask (uint16_t)
//   misc_buttons    MISC_BUTTON_SELECT/START bitmask (uint8_t)
//   dpad            DPAD_UP/DOWN/LEFT/RIGHT bitmask (uint8_t)
//
// Dreamcast condition packet is active-low (0 = pressed).

#include "hid_map.h"
#include "controller.h"
#include "maple/vmu.h"

#include <controller/uni_gamepad.h>

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

// ─────────────────────────────────────────────────────────────────────────────
// Axis / trigger conversion helpers
// ─────────────────────────────────────────────────────────────────────────────

// Bluepad32 axis: –512..+511  →  Dreamcast 0..255 (128 = centre)
static uint8_t axis_bp32_to_dc(int32_t v) {
    if (v < -512) v = -512;
    if (v >  511) v =  511;
    return (uint8_t)((v + 512) * 255 / 1023);
}

// Bluepad32 trigger: 0..1023  →  Dreamcast 0..255
static uint8_t trigger_bp32_to_dc(int32_t v) {
    if (v < 0)    v = 0;
    if (v > 1023) v = 1023;
    return (uint8_t)(v * 255 / 1023);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mode switching / combo detection
// ─────────────────────────────────────────────────────────────────────────────
// Returns true if a mode-switch combo was consumed; caller should output neutral.

static bool check_combos(uint16_t btns, uint8_t misc) {
    // "Select / Share / Create / Minus / Back" acts as the combo modifier.
    bool sel = (misc & MISC_BUTTON_SELECT) != 0;
    if (!sel) return false;

    bool a  = (btns & BUTTON_A)          != 0;
    bool b  = (btns & BUTTON_B)          != 0;
    bool x  = (btns & BUTTON_X)          != 0;
    bool y  = (btns & BUTTON_Y)          != 0;
    bool lb = (btns & BUTTON_SHOULDER_L) != 0;
    bool rb = (btns & BUTTON_SHOULDER_R) != 0;

    if (a)  { controller_set_mode(CTRL_MODE_STANDARD);    return true; }
    if (b)  { controller_set_mode(CTRL_MODE_DUAL_ANALOG); return true; }
    if (x)  { controller_set_mode(CTRL_MODE_TWIN_STICK);  return true; }
    if (y)  { controller_set_mode(CTRL_MODE_FIGHT_STICK); return true; }
    if (lb) { controller_set_mode(CTRL_MODE_RACING);      return true; }
    if (rb) {
        uint8_t next = (vmu_get_bank() + 1) % 10;
        vmu_set_bank(next);
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Stick routing by mode
// ─────────────────────────────────────────────────────────────────────────────

static void apply_mode_sticks(dc_controller_state_t *s,
                               uint8_t lx, uint8_t ly,
                               uint8_t rx, uint8_t ry) {
    ctrl_mode_t mode = controller_get_mode();
    switch (mode) {
        case CTRL_MODE_DUAL_ANALOG:
            s->joy_x  = lx; s->joy_y  = ly;
            s->joy2_x = rx; s->joy2_y = ry;
            break;
        case CTRL_MODE_TWIN_STICK:
            s->joy_x  = lx; s->joy_y  = ly;
            s->joy2_x = rx; s->joy2_y = ry;
            break;
        case CTRL_MODE_RACING:
            // Left stick X → steering; sticks 2 unused
            s->joy_x  = lx;
            s->joy_y  = DC_STICK_CENTER;
            s->joy2_x = DC_STICK_CENTER;
            s->joy2_y = DC_STICK_CENTER;
            break;
        case CTRL_MODE_FIGHT_STICK:
            // D-pad drives movement; sticks ignored
            s->joy_x  = DC_STICK_CENTER; s->joy_y  = DC_STICK_CENTER;
            s->joy2_x = DC_STICK_CENTER; s->joy2_y = DC_STICK_CENTER;
            break;
        default: // CTRL_MODE_STANDARD
            s->joy_x  = lx; s->joy_y  = ly;
            // Right stick → D-pad emulation
            if (rx < 0x40) s->buttons &= ~DC_BTN_DPAD_LEFT;
            if (rx > 0xC0) s->buttons &= ~DC_BTN_DPAD_RIGHT;
            if (ry < 0x40) s->buttons &= ~DC_BTN_DPAD_UP;
            if (ry > 0xC0) s->buttons &= ~DC_BTN_DPAD_DOWN;
            s->joy2_x = DC_STICK_CENTER;
            s->joy2_y = DC_STICK_CENTER;
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Public entry point — called from bt.c on_controller_data
// ─────────────────────────────────────────────────────────────────────────────

void hid_map_gamepad(const uni_gamepad_t *gp, dc_controller_state_t *s) {
    // Start with all buttons released (active-low: 1 = not pressed)
    s->buttons       = DC_BUTTONS_RELEASED;
    s->left_trigger  = DC_TRIGGER_RELEASED;
    s->right_trigger = DC_TRIGGER_RELEASED;
    s->joy_x  = s->joy_y  = DC_STICK_CENTER;
    s->joy2_x = s->joy2_y = DC_STICK_CENTER;

    uint16_t btns = gp->buttons;
    uint8_t  misc = gp->misc_buttons;
    uint8_t  dpad = gp->dpad;

    // ── Mode / combo check ─────────────────────────────────────────────────
    if (check_combos(btns, misc)) {
        // Combo consumed — output neutral state this frame
        return;
    }

    // ── D-pad (dedicated dpad byte in Bluepad32) ───────────────────────────
    if (dpad & DPAD_UP)    s->buttons &= ~DC_BTN_DPAD_UP;
    if (dpad & DPAD_DOWN)  s->buttons &= ~DC_BTN_DPAD_DOWN;
    if (dpad & DPAD_LEFT)  s->buttons &= ~DC_BTN_DPAD_LEFT;
    if (dpad & DPAD_RIGHT) s->buttons &= ~DC_BTN_DPAD_RIGHT;

    // ── Face buttons ───────────────────────────────────────────────────────
    // Bluepad32 uses Xbox naming: A=bottom, B=right, X=left, Y=top
    if (btns & BUTTON_A) s->buttons &= ~DC_BTN_A;
    if (btns & BUTTON_B) s->buttons &= ~DC_BTN_B;
    if (btns & BUTTON_X) s->buttons &= ~DC_BTN_X;
    if (btns & BUTTON_Y) s->buttons &= ~DC_BTN_Y;

    // ── Shoulder buttons → DC Z (L1) and C (R1) ───────────────────────────
    if (btns & BUTTON_SHOULDER_L) s->buttons &= ~DC_BTN_Z;
    if (btns & BUTTON_SHOULDER_R) s->buttons &= ~DC_BTN_C;

    // ── Start ─────────────────────────────────────────────────────────────
    if (misc & MISC_BUTTON_START) s->buttons &= ~DC_BTN_START;

    // ── Analogue triggers → DC L2 / R2 (0-255) ────────────────────────────
    // Bluepad32: brake = L2/LT, throttle = R2/RT  (0..1023)
    s->left_trigger  = trigger_bp32_to_dc(gp->brake);
    s->right_trigger = trigger_bp32_to_dc(gp->throttle);

    // ── Sticks ────────────────────────────────────────────────────────────
    uint8_t lx = axis_bp32_to_dc(gp->axis_x);
    uint8_t ly = axis_bp32_to_dc(gp->axis_y);
    uint8_t rx = axis_bp32_to_dc(gp->axis_rx);
    uint8_t ry = axis_bp32_to_dc(gp->axis_ry);
    apply_mode_sticks(s, lx, ly, rx, ry);
}
