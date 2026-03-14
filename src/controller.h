#pragma once

#include <stdint.h>
#include "pico/sync.h"

// ── Dreamcast Controller State ────────────────────────────────────────────────
// Layout matches the Maple GET_CONDITION response payload exactly.
// Buttons are active LOW: 0 = pressed, 1 = released.
typedef struct __attribute__((packed)) {
    uint16_t buttons;           // Button bitmask — see DC_BTN_* below
    uint8_t  right_trigger;     // 0x00 = released, 0xFF = fully pressed
    uint8_t  left_trigger;
    uint8_t  joy_x;             // 0x00 = full left, 0x80 = centre, 0xFF = full right
    uint8_t  joy_y;             // 0x00 = full up,   0x80 = centre, 0xFF = full down
    uint8_t  joy2_x;            // Second analog stick (unused if not present)
    uint8_t  joy2_y;
} dc_controller_state_t;

// Button bitmask — bit set means NOT pressed (active low)
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
// Bits 12-15: reserved, keep set (not pressed)

// Neutral / default state
#define DC_BUTTONS_RELEASED 0x0FFFu
#define DC_STICK_CENTER     0x80u
#define DC_TRIGGER_RELEASED 0x00u

// ── Shared Global State ───────────────────────────────────────────────────────
// Core 1 (Bluetooth) writes; Core 0 (Maple) reads.
// Protected by g_controller_spinlock.
extern volatile dc_controller_state_t g_controller_state;
extern spin_lock_t *g_controller_spinlock;

// Call from main() before launching cores.
void controller_state_init(void);

// Safe write from Core 1.
void controller_state_update(const dc_controller_state_t *state);

// Safe read from Core 0.
void controller_state_read(dc_controller_state_t *dst);
