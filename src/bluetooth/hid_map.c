#include "hid_map.h"
#include "maple/vmu.h"
#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────────────────────────────────────
// Common helpers
// ─────────────────────────────────────────────────────────────────────────────

// Check for mode-switch or VMU-bank combos and act on them.
// Returns true if the combo was consumed (caller should not send input to DC).
static bool check_combos(bool select, bool a, bool b, bool x, bool y,
                          bool lb, bool rb) {
    if (!select) return false;
    if (a)  { controller_set_mode(CTRL_MODE_STANDARD);   return true; }
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

static void apply_dpad_hat8(dc_controller_state_t *s, uint8_t hat) {
    // hat: 0=N,1=NE,2=E,3=SE,4=S,5=SW,6=W,7=NW,8+=none
    if (hat == 0 || hat == 1 || hat == 7) s->buttons &= ~DC_BTN_DPAD_UP;
    if (hat == 3 || hat == 4 || hat == 5) s->buttons &= ~DC_BTN_DPAD_DOWN;
    if (hat == 5 || hat == 6 || hat == 7) s->buttons &= ~DC_BTN_DPAD_LEFT;
    if (hat == 1 || hat == 2 || hat == 3) s->buttons &= ~DC_BTN_DPAD_RIGHT;
}

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
            // Left stick → main joystick; right stick → joy2
            s->joy_x  = lx; s->joy_y  = ly;
            s->joy2_x = rx; s->joy2_y = ry;
            break;
        case CTRL_MODE_RACING:
            // Left stick X → steering (joy_x), left trigger = brake, right = accel
            s->joy_x  = lx;
            s->joy_y  = DC_STICK_CENTER;
            s->joy2_x = DC_STICK_CENTER;
            s->joy2_y = DC_STICK_CENTER;
            break;
        case CTRL_MODE_FIGHT_STICK:
            // D-pad primary; sticks ignored
            s->joy_x  = DC_STICK_CENTER; s->joy_y  = DC_STICK_CENTER;
            s->joy2_x = DC_STICK_CENTER; s->joy2_y = DC_STICK_CENTER;
            break;
        default: // STANDARD
            s->joy_x  = lx; s->joy_y  = ly;
            // Right stick → D-pad emulation in standard mode
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
// DS4 (DualShock 4) — BT report ID 0x11
// ─────────────────────────────────────────────────────────────────────────────
#define DS4_REPORT_ID 0x11

static void map_ds4(const uint8_t *d, uint16_t len, dc_controller_state_t *s) {
    if (len < 10) return;
    s->buttons = DC_BUTTONS_RELEASED;

    uint8_t hat  = d[4] & 0x0F;
    bool square  = d[4] & (1<<4), cross   = d[4] & (1<<5);
    bool circle  = d[4] & (1<<6), tri     = d[4] & (1<<7);
    bool l1      = d[5] & (1<<0), r1      = d[5] & (1<<1);
    bool share   = d[5] & (1<<4), options = d[5] & (1<<5);

    if (check_combos(share || options, cross, circle, square, tri, l1, r1)) {
        s->buttons = DC_BUTTONS_RELEASED;
        s->right_trigger = s->left_trigger = 0;
        s->joy_x = s->joy_y = s->joy2_x = s->joy2_y = DC_STICK_CENTER;
        return;
    }

    apply_dpad_hat8(s, hat);
    if (square)  s->buttons &= ~DC_BTN_X;
    if (cross)   s->buttons &= ~DC_BTN_A;
    if (circle)  s->buttons &= ~DC_BTN_B;
    if (tri)     s->buttons &= ~DC_BTN_Y;
    if (l1)      s->buttons &= ~DC_BTN_Z;
    if (r1)      s->buttons &= ~DC_BTN_C;
    if (options) s->buttons &= ~DC_BTN_START;

    s->left_trigger  = d[7];
    s->right_trigger = d[8];
    apply_mode_sticks(s, d[0], d[1], d[2], d[3]);
}

// ─────────────────────────────────────────────────────────────────────────────
// DS5 (DualSense PS5) — BT report ID 0x31
// Byte layout: 0=lx,1=ly,2=rx,3=ry,4=hat+btns,5=btns2,6=btns3,7=l2,8=r2
// ─────────────────────────────────────────────────────────────────────────────
#define DS5_REPORT_ID 0x31

static void map_ds5(const uint8_t *d, uint16_t len, dc_controller_state_t *s) {
    if (len < 10) return;
    s->buttons = DC_BUTTONS_RELEASED;

    uint8_t hat  = d[4] & 0x0F;
    bool square  = d[4] & (1<<4), cross   = d[4] & (1<<5);
    bool circle  = d[4] & (1<<6), tri     = d[4] & (1<<7);
    bool l1      = d[5] & (1<<0), r1      = d[5] & (1<<1);
    bool create  = d[5] & (1<<4), options = d[5] & (1<<5);

    if (check_combos(create || options, cross, circle, square, tri, l1, r1)) {
        s->buttons = DC_BUTTONS_RELEASED;
        s->right_trigger = s->left_trigger = 0;
        s->joy_x = s->joy_y = s->joy2_x = s->joy2_y = DC_STICK_CENTER;
        return;
    }

    apply_dpad_hat8(s, hat);
    if (square)  s->buttons &= ~DC_BTN_X;
    if (cross)   s->buttons &= ~DC_BTN_A;
    if (circle)  s->buttons &= ~DC_BTN_B;
    if (tri)     s->buttons &= ~DC_BTN_Y;
    if (l1)      s->buttons &= ~DC_BTN_Z;
    if (r1)      s->buttons &= ~DC_BTN_C;
    if (options) s->buttons &= ~DC_BTN_START;

    s->left_trigger  = d[7];
    s->right_trigger = d[8];
    apply_mode_sticks(s, d[0], d[1], d[2], d[3]);
}

// ─────────────────────────────────────────────────────────────────────────────
// Xbox One / Series — BT report ID 0x01 (standard HID gamepad layout)
// Byte layout (BT HID):
//   0-1: buttons bitmask
//   2:   trigger left  (0-255)
//   3:   trigger right (0-255)
//   4-5: left  stick X (int16_t, centre=0)
//   6-7: left  stick Y (int16_t, centre=0)
//   8-9: right stick X
//  10-11: right stick Y
// ─────────────────────────────────────────────────────────────────────────────
#define XBOX_REPORT_ID 0x01

static uint8_t axis_s16_to_u8(int16_t v) {
    // int16_t [-32768..32767] → uint8_t [0..255], centre=0x80
    return (uint8_t)((v / 256) + 128);
}

static void map_xbox(const uint8_t *d, uint16_t len, dc_controller_state_t *s) {
    if (len < 12) return;
    s->buttons = DC_BUTTONS_RELEASED;

    uint16_t btns = (uint16_t)(d[0] | (d[1] << 8));
    // Xbox button bitmask (standard HID usage page):
    //   bit 0=A, 1=B, 2=X, 3=Y, 4=LB, 5=RB, 6=view(select), 7=menu(start)
    //   bit 8=LS, 9=RS, 12=dpad_up, 13=dpad_down, 14=dpad_left, 15=dpad_right
    bool a     = btns & (1<<0),  b    = btns & (1<<1);
    bool x     = btns & (1<<2),  y    = btns & (1<<3);
    bool lb    = btns & (1<<4),  rb   = btns & (1<<5);
    bool view  = btns & (1<<6),  menu = btns & (1<<7);
    bool dup   = btns & (1<<12), ddn  = btns & (1<<13);
    bool dleft = btns & (1<<14), drt  = btns & (1<<15);

    if (check_combos(view, a, b, x, y, lb, rb)) {
        s->buttons = DC_BUTTONS_RELEASED;
        s->right_trigger = s->left_trigger = 0;
        s->joy_x = s->joy_y = s->joy2_x = s->joy2_y = DC_STICK_CENTER;
        return;
    }

    if (dup)   s->buttons &= ~DC_BTN_DPAD_UP;
    if (ddn)   s->buttons &= ~DC_BTN_DPAD_DOWN;
    if (dleft) s->buttons &= ~DC_BTN_DPAD_LEFT;
    if (drt)   s->buttons &= ~DC_BTN_DPAD_RIGHT;
    if (a)     s->buttons &= ~DC_BTN_A;
    if (b)     s->buttons &= ~DC_BTN_B;
    if (x)     s->buttons &= ~DC_BTN_X;
    if (y)     s->buttons &= ~DC_BTN_Y;
    if (lb)    s->buttons &= ~DC_BTN_Z;
    if (rb)    s->buttons &= ~DC_BTN_C;
    if (menu)  s->buttons &= ~DC_BTN_START;

    s->left_trigger  = d[2];
    s->right_trigger = d[3];

    int16_t lx = (int16_t)(d[4]  | (d[5]  << 8));
    int16_t ly = (int16_t)(d[6]  | (d[7]  << 8));
    int16_t rx = (int16_t)(d[8]  | (d[9]  << 8));
    int16_t ry = (int16_t)(d[10] | (d[11] << 8));
    apply_mode_sticks(s, axis_s16_to_u8(lx), axis_s16_to_u8(ly),
                         axis_s16_to_u8(rx), axis_s16_to_u8(ry));
}

// ─────────────────────────────────────────────────────────────────────────────
// Nintendo Switch Pro Controller — BT report ID 0x30
// Byte layout:
//   0:   timer
//   1:   connection info / battery
//   2:   right buttons  (Y=0,X=1,B=2,A=3,SR=4,SL=5,R=6,ZR=7)
//   3:   misc  (minus=0,plus=1,RS=2,LS=3,home=4,cap=5)
//   4:   left  buttons  (down=0,up=1,right=2,left=3,SR=4,SL=5,L=6,ZL=7)
//   5-7: left  stick  (12-bit, packed)
//   8-10: right stick (12-bit, packed)
// ─────────────────────────────────────────────────────────────────────────────
#define SWITCH_REPORT_ID 0x30

static uint8_t switch_axis(uint16_t raw) {
    // Switch sticks are 12-bit centred at ~0x800; map to 0-255 u8
    // Clamp to [0, 4095] first
    if (raw > 4095) raw = 4095;
    return (uint8_t)(raw >> 4);
}

static void map_switch(const uint8_t *d, uint16_t len, dc_controller_state_t *s) {
    if (len < 11) return;
    s->buttons = DC_BUTTONS_RELEASED;

    uint8_t rb = d[2], misc = d[3], lb = d[4];
    bool y    = rb & (1<<0), x  = rb & (1<<1);
    bool b    = rb & (1<<2), a  = rb & (1<<3);
    bool r    = rb & (1<<6), zr = rb & (1<<7);
    bool plus = misc & (1<<1), home = misc & (1<<4);
    bool down = lb & (1<<0),  up   = lb & (1<<1);
    bool right= lb & (1<<2),  left = lb & (1<<3);
    bool l    = lb & (1<<6),  zl   = lb & (1<<7);
    bool minus= misc & (1<<0);

    if (check_combos(minus, b, a, y, x, l, r)) {
        s->buttons = DC_BUTTONS_RELEASED;
        s->right_trigger = s->left_trigger = 0;
        s->joy_x = s->joy_y = s->joy2_x = s->joy2_y = DC_STICK_CENTER;
        return;
    }

    if (up)    s->buttons &= ~DC_BTN_DPAD_UP;
    if (down)  s->buttons &= ~DC_BTN_DPAD_DOWN;
    if (left)  s->buttons &= ~DC_BTN_DPAD_LEFT;
    if (right) s->buttons &= ~DC_BTN_DPAD_RIGHT;
    if (b)     s->buttons &= ~DC_BTN_A;    // B → A  (Nintendo B = bottom)
    if (a)     s->buttons &= ~DC_BTN_B;    // A → B
    if (y)     s->buttons &= ~DC_BTN_X;
    if (x)     s->buttons &= ~DC_BTN_Y;
    if (l)     s->buttons &= ~DC_BTN_Z;
    if (r)     s->buttons &= ~DC_BTN_C;
    if (plus)  s->buttons &= ~DC_BTN_START;
    s->left_trigger  = zl ? 0xFF : 0x00;
    s->right_trigger = zr ? 0xFF : 0x00;

    // Packed 12-bit sticks
    uint16_t lx_raw = (uint16_t)(d[5] | ((d[6] & 0x0F) << 8));
    uint16_t ly_raw = (uint16_t)((d[6] >> 4) | (d[7] << 4));
    uint16_t rx_raw = (uint16_t)(d[8] | ((d[9] & 0x0F) << 8));
    uint16_t ry_raw = (uint16_t)((d[9] >> 4) | (d[10] << 4));
    apply_mode_sticks(s, switch_axis(lx_raw), switch_axis(ly_raw),
                         switch_axis(rx_raw), switch_axis(ry_raw));
}

// ─────────────────────────────────────────────────────────────────────────────
// 8BitDo (various) — uses standard HID gamepad report ID 0x03
// Most 8BitDo controllers in X-input/D-input mode follow this layout:
//   0-1: buttons, 2: hat, 3-10: axes (lx,ly,rx,ry,lt,rt as u8)
// ─────────────────────────────────────────────────────────────────────────────
#define BITDO_REPORT_ID 0x03

static void map_8bitdo(const uint8_t *d, uint16_t len, dc_controller_state_t *s) {
    if (len < 11) return;
    s->buttons = DC_BUTTONS_RELEASED;

    uint16_t btns = (uint16_t)(d[0] | (d[1] << 8));
    bool b   = btns & (1<<0), a  = btns & (1<<1);
    bool y   = btns & (1<<2), x  = btns & (1<<3);
    bool lb  = btns & (1<<4), rb = btns & (1<<5);
    bool sel = btns & (1<<6), st = btns & (1<<7);
    uint8_t hat = d[2];

    if (check_combos(sel, a, b, y, x, lb, rb)) {
        s->buttons = DC_BUTTONS_RELEASED;
        s->right_trigger = s->left_trigger = 0;
        s->joy_x = s->joy_y = s->joy2_x = s->joy2_y = DC_STICK_CENTER;
        return;
    }

    apply_dpad_hat8(s, hat);
    if (b)  s->buttons &= ~DC_BTN_A;
    if (a)  s->buttons &= ~DC_BTN_B;
    if (y)  s->buttons &= ~DC_BTN_X;
    if (x)  s->buttons &= ~DC_BTN_Y;
    if (lb) s->buttons &= ~DC_BTN_Z;
    if (rb) s->buttons &= ~DC_BTN_C;
    if (st) s->buttons &= ~DC_BTN_START;

    s->left_trigger  = d[8];
    s->right_trigger = d[9];
    apply_mode_sticks(s, d[3], d[4], d[5], d[6]);
}

// ─────────────────────────────────────────────────────────────────────────────
// Generic / fallback
// ─────────────────────────────────────────────────────────────────────────────
static void map_generic(dc_controller_state_t *s) {
    dc_controller_state_t neutral = DC_STATE_NEUTRAL;
    *s = neutral;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public entry point
// ─────────────────────────────────────────────────────────────────────────────
void hid_map_report(const uint8_t *report, uint16_t len,
                    dc_controller_state_t *state) {
    if (len < 2) return;

    uint8_t report_id  = report[0];
    const uint8_t *data = report + 1;
    uint16_t data_len   = len - 1;

    switch (report_id) {
        case DS4_REPORT_ID:    map_ds4(data, data_len, state);    break;
        case DS5_REPORT_ID:    map_ds5(data, data_len, state);    break;
        case XBOX_REPORT_ID:   map_xbox(data, data_len, state);   break;
        case SWITCH_REPORT_ID: map_switch(data, data_len, state); break;
        case BITDO_REPORT_ID:  map_8bitdo(data, data_len, state); break;
        default:
            printf("[hid_map] unknown report ID 0x%02X\n", report_id);
            map_generic(state);
            break;
    }
}
