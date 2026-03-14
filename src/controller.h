#pragma once

#include <stdint.h>
#include "pico/sync.h"

// ── Controller Modes ──────────────────────────────────────────────────────────
// Switch mode by holding SELECT + face button for 1 second.
//   SELECT + A  → STANDARD      (default; right stick = D-pad when no left)
//   SELECT + B  → DUAL_ANALOG   (both sticks active, joy2 = second analog)
//   SELECT + X  → TWIN_STICK    (left stick = joystick 1, right stick = joystick 2)
//   SELECT + Y  → FIGHT_STICK   (D-pad primary, face buttons remapped for fighters)
//   SELECT + LB → RACING        (advertises as racing wheel, left stick = steering)
//   SELECT + RB → next VMU bank (cycles 0-9)
typedef enum {
    CTRL_MODE_STANDARD   = 0,
    CTRL_MODE_DUAL_ANALOG,
    CTRL_MODE_TWIN_STICK,
    CTRL_MODE_FIGHT_STICK,
    CTRL_MODE_RACING,
    CTRL_MODE_COUNT
} ctrl_mode_t;

// ── Dreamcast Controller State ────────────────────────────────────────────────
// Layout matches the Maple GET_CONDITION response payload exactly.
// Buttons are active LOW: 0 = pressed, 1 = released.
typedef struct __attribute__((packed)) {
    uint16_t buttons;
    uint8_t  right_trigger;
    uint8_t  left_trigger;
    uint8_t  joy_x;
    uint8_t  joy_y;
    uint8_t  joy2_x;
    uint8_t  joy2_y;
} dc_controller_state_t;

// Button bitmask — bit SET means NOT pressed (active low)
#define DC_BTN_C            (1u << 0)
#define DC_BTN_B            (1u << 1)
#define DC_BTN_A            (1u << 2)
#define DC_BTN_START        (1u << 3)
#define DC_BTN_DPAD_UP      (1u << 4)
#define DC_BTN_DPAD_DOWN    (1u << 5)
#define DC_BTN_DPAD_LEFT    (1u << 6)
#define DC_BTN_DPAD_RIGHT   (1u << 7)
#define DC_BTN_Z            (1u << 8)
#define DC_BTN_Y            (1u << 9)
#define DC_BTN_X            (1u << 10)
#define DC_BTN_D            (1u << 11)

#define DC_BUTTONS_RELEASED 0x0FFFu
#define DC_STICK_CENTER     0x80u
#define DC_TRIGGER_RELEASED 0x00u

// ── Neutral State ─────────────────────────────────────────────────────────────
#define DC_STATE_NEUTRAL { \
    .buttons       = DC_BUTTONS_RELEASED, \
    .right_trigger = DC_TRIGGER_RELEASED, \
    .left_trigger  = DC_TRIGGER_RELEASED, \
    .joy_x         = DC_STICK_CENTER,     \
    .joy_y         = DC_STICK_CENTER,     \
    .joy2_x        = DC_STICK_CENTER,     \
    .joy2_y        = DC_STICK_CENTER,     \
}

// ── Shared Global State ───────────────────────────────────────────────────────
// Core 1 (Bluetooth) writes; Core 0 (Maple) reads.
extern volatile dc_controller_state_t g_controller_state;
extern spin_lock_t *g_controller_spinlock;

// Rumble intensity: 0 = off, 255 = full.  Written by maple.c, read by bt.c.
extern volatile uint8_t g_rumble_intensity;

// Current mode — read by hid_map.c when translating reports.
extern volatile ctrl_mode_t g_ctrl_mode;

void controller_state_init(void);
void controller_state_update(const dc_controller_state_t *state);
void controller_state_read(dc_controller_state_t *dst);

// Called by maple.c when a SET_CONDITION (rumble) command arrives.
void controller_set_rumble(uint8_t intensity);

// Called by hid_map.c when a mode-switch combo is detected.
void controller_set_mode(ctrl_mode_t mode);
ctrl_mode_t controller_get_mode(void);
