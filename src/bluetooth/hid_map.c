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
#include "config_store.h"
#include "maple/vmu.h"

#include <controller/uni_gamepad.h>

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "pico/time.h"

// ─────────────────────────────────────────────────────────────────────────────
// Axis / trigger conversion helpers (deadzone-aware)
// ─────────────────────────────────────────────────────────────────────────────

// Apply inner/outer deadzone, then scale to DC 0..255 (128 = centre).
// inner_pct: % of full range within which output = centre.
// outer_pct: % of full range beyond which output = max.
static uint8_t axis_with_dz(int32_t v, uint8_t inner_pct, uint8_t outer_pct) {
    if (v < -512) v = -512;
    if (v >  511) v =  511;

    int32_t inner = (int32_t)inner_pct * 512 / 100;
    int32_t outer = (int32_t)outer_pct * 512 / 100;
    if (outer <= inner) outer = inner + 1;

    int32_t sign = (v >= 0) ? 1 : -1;
    int32_t mag  = (v < 0) ? -v : v;

    if (mag <= inner) return 0x80;
    if (mag >= outer) return (sign > 0) ? 0xFF : 0x00;

    int32_t scaled = (mag - inner) * 127 / (outer - inner);
    int32_t dc = 0x80 + sign * scaled;
    if (dc < 0)   dc = 0;
    if (dc > 255) dc = 255;
    return (uint8_t)dc;
}

// Bluepad32 trigger: 0..1023  →  Dreamcast 0..255
static uint8_t trigger_bp32_to_dc(int32_t v) {
    if (v < 0)    v = 0;
    if (v > 1023) v = 1023;
    return (uint8_t)(v * 255 / 1023);
}

// ─────────────────────────────────────────────────────────────────────────────
// Source-button query helpers
// ─────────────────────────────────────────────────────────────────────────────

static bool src_pressed(uint8_t src, const uni_gamepad_t *gp, uint8_t trig_thresh) {
    switch (src) {
        case SRC_BTN_A:          return (gp->buttons & BUTTON_A)          != 0;
        case SRC_BTN_B:          return (gp->buttons & BUTTON_B)          != 0;
        case SRC_BTN_X:          return (gp->buttons & BUTTON_X)          != 0;
        case SRC_BTN_Y:          return (gp->buttons & BUTTON_Y)          != 0;
        case SRC_BTN_L_SHOULDER: return (gp->buttons & BUTTON_SHOULDER_L) != 0;
        case SRC_BTN_R_SHOULDER: return (gp->buttons & BUTTON_SHOULDER_R) != 0;
        case SRC_BTN_L_TRIGGER:  return trigger_bp32_to_dc(gp->brake)    > trig_thresh;
        case SRC_BTN_R_TRIGGER:  return trigger_bp32_to_dc(gp->throttle) > trig_thresh;
        case SRC_BTN_SELECT:     return (gp->misc_buttons & MISC_BUTTON_SELECT) != 0;
        case SRC_BTN_START:      return (gp->misc_buttons & MISC_BUTTON_START)  != 0;
        case SRC_DPAD_UP:        return (gp->dpad & DPAD_UP)    != 0;
        case SRC_DPAD_DOWN:      return (gp->dpad & DPAD_DOWN)  != 0;
        case SRC_DPAD_LEFT:      return (gp->dpad & DPAD_LEFT)  != 0;
        case SRC_DPAD_RIGHT:     return (gp->dpad & DPAD_RIGHT) != 0;
        default:                 return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Rapid-fire state
// ─────────────────────────────────────────────────────────────────────────────

static bool s_rf_phase = false;
static absolute_time_t s_rf_next = {0};

static bool rf_on(uint8_t hz) {
    if (is_nil_time(s_rf_next)) s_rf_next = get_absolute_time();
    if (absolute_time_diff_us(s_rf_next, get_absolute_time()) >= 0) {
        s_rf_phase = !s_rf_phase;
        s_rf_next  = make_timeout_time_ms(1000 / ((uint32_t)hz * 2));
    }
    return s_rf_phase;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mode switching / combo detection
// ─────────────────────────────────────────────────────────────────────────────

static bool check_combos(uint16_t btns, uint8_t misc) {
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
        case CTRL_MODE_TWIN_STICK:
            s->joy_x  = lx; s->joy_y  = ly;
            s->joy2_x = rx; s->joy2_y = ry;
            break;
        case CTRL_MODE_RACING:
            s->joy_x  = lx;
            s->joy_y  = DC_STICK_CENTER;
            s->joy2_x = DC_STICK_CENTER;
            s->joy2_y = DC_STICK_CENTER;
            break;
        case CTRL_MODE_FIGHT_STICK:
            s->joy_x  = DC_STICK_CENTER; s->joy_y  = DC_STICK_CENTER;
            s->joy2_x = DC_STICK_CENTER; s->joy2_y = DC_STICK_CENTER;
            break;
        default: // CTRL_MODE_STANDARD
            s->joy_x  = lx; s->joy_y  = ly;
            // Right stick → D-pad override (only when no physical d-pad input)
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
// DC button mask lookup
// ─────────────────────────────────────────────────────────────────────────────

static const uint16_t k_dc_mask[DC_NUM_BUTTONS] = {
    DC_BTN_C, DC_BTN_B, DC_BTN_A, DC_BTN_START,
    DC_BTN_DPAD_UP, DC_BTN_DPAD_DOWN, DC_BTN_DPAD_LEFT, DC_BTN_DPAD_RIGHT,
    DC_BTN_Z, DC_BTN_Y, DC_BTN_X, DC_BTN_D,
};

// ─────────────────────────────────────────────────────────────────────────────
// Public entry point — called from bt.c on_controller_data
// ─────────────────────────────────────────────────────────────────────────────

void hid_map_gamepad(const uni_gamepad_t *gp, dc_controller_state_t *s) {
    s->buttons       = DC_BUTTONS_RELEASED;
    s->left_trigger  = DC_TRIGGER_RELEASED;
    s->right_trigger = DC_TRIGGER_RELEASED;
    s->joy_x  = s->joy_y  = DC_STICK_CENTER;
    s->joy2_x = s->joy2_y = DC_STICK_CENTER;

    uint16_t btns = gp->buttons;
    uint8_t  misc = gp->misc_buttons;

    // ── Mode / combo check ────────────────────────────────────────────────
    if (check_combos(btns, misc)) return;

    // ── Load runtime config ───────────────────────────────────────────────
    global_cfg_t *gcfg = config_global();

    // Per-game config: if found, override ctrl_mode and use custom remap.
    // btn_remap is now uint32_t[] (nanopb); we read per-slot values inline.
    game_cfg_t *pgcfg = config_game_by_hash(g_current_game_hash);
    if (pgcfg && pgcfg->ctrl_mode != 0xFF)
        controller_set_mode((ctrl_mode_t)pgcfg->ctrl_mode);

    // ── Button mapping ────────────────────────────────────────────────────
    for (int slot = 0; slot < DC_NUM_BUTTONS; slot++) {
        uint8_t src;
        if (pgcfg && (pb_size_t)slot < pgcfg->btn_remap_count)
            src = (uint8_t)pgcfg->btn_remap[slot];
        else
            src = k_default_remap[slot];
        if (src == SRC_BTN_NONE) src = k_default_remap[slot]; // fall back to default
        if (src == SRC_BTN_NONE || src >= SRC_BTN_COUNT) continue;

        if (src_pressed(src, gp, (uint8_t)gcfg->trigger_threshold)) {
            bool fire = true;
            if (gcfg->rapid_fire_mask & (1u << slot))
                fire = rf_on(gcfg->rapid_fire_hz ? gcfg->rapid_fire_hz : 10);
            if (fire)
                s->buttons &= ~k_dc_mask[slot];
        }
    }

    // ── Analogue triggers → DC L/R (0-255) ───────────────────────────────
    s->left_trigger  = trigger_bp32_to_dc(gp->brake);
    s->right_trigger = trigger_bp32_to_dc(gp->throttle);

    // ── Sticks ────────────────────────────────────────────────────────────
    int32_t lx_raw = gcfg->invert_lx ? -gp->axis_x  : gp->axis_x;
    int32_t ly_raw = gcfg->invert_ly ? -gp->axis_y  : gp->axis_y;
    int32_t rx_raw = gcfg->invert_rx ? -gp->axis_rx : gp->axis_rx;
    int32_t ry_raw = gcfg->invert_ry ? -gp->axis_ry : gp->axis_ry;

    uint8_t lx = axis_with_dz(lx_raw, (uint8_t)gcfg->deadzone_inner, (uint8_t)gcfg->deadzone_outer);
    uint8_t ly = axis_with_dz(ly_raw, (uint8_t)gcfg->deadzone_inner, (uint8_t)gcfg->deadzone_outer);
    uint8_t rx = axis_with_dz(rx_raw, (uint8_t)gcfg->deadzone_inner, (uint8_t)gcfg->deadzone_outer);
    uint8_t ry = axis_with_dz(ry_raw, (uint8_t)gcfg->deadzone_inner, (uint8_t)gcfg->deadzone_outer);

    apply_mode_sticks(s, lx, ly, rx, ry);
}
