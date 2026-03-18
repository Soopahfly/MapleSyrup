#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "controller.h"  // for ctrl_mode_t / CTRL_MODE_COUNT

// ── Source button indices for remapping ──────────────────────────────────────
// These are the inputs a DC button slot can be remapped to.
#define SRC_BTN_A           0
#define SRC_BTN_B           1
#define SRC_BTN_X           2
#define SRC_BTN_Y           3
#define SRC_BTN_L_SHOULDER  4
#define SRC_BTN_R_SHOULDER  5
#define SRC_BTN_L_TRIGGER   6   // digital, fires when > trigger_threshold
#define SRC_BTN_R_TRIGGER   7   // digital, fires when > trigger_threshold
#define SRC_BTN_SELECT      8
#define SRC_BTN_START       9
#define SRC_DPAD_UP         10
#define SRC_DPAD_DOWN       11
#define SRC_DPAD_LEFT       12
#define SRC_DPAD_RIGHT      13
#define SRC_BTN_COUNT       14
#define SRC_BTN_NONE        0xFF  // unassigned

// String names used by the config tool protocol
extern const char *const k_src_btn_names[SRC_BTN_COUNT];

// ── DC button slot indices ────────────────────────────────────────────────────
// Order matches the bit positions in dc_controller_state_t.buttons.
#define DC_SLOT_C       0
#define DC_SLOT_B       1
#define DC_SLOT_A       2
#define DC_SLOT_START   3
#define DC_SLOT_UP      4
#define DC_SLOT_DOWN    5
#define DC_SLOT_LEFT    6
#define DC_SLOT_RIGHT   7
#define DC_SLOT_Z       8
#define DC_SLOT_Y       9
#define DC_SLOT_X       10
#define DC_SLOT_D       11
#define DC_NUM_BUTTONS  12

extern const char *const k_dc_slot_names[DC_NUM_BUTTONS];

// ── Per-game config ───────────────────────────────────────────────────────────
#define MAX_GAME_CFGS 32

typedef struct __attribute__((packed)) {
    uint32_t hash;                  // FNV-1a of game_id bytes; 0 = empty slot
    char     name[28];              // user-editable friendly name + NUL
    uint8_t  vmu_bank;              // 0-9; 0xFF = auto (hash-based)
    uint8_t  ctrl_mode;             // CTRL_MODE_*; 0xFF = use global default
    uint8_t  btn_remap[DC_NUM_BUTTONS]; // SRC_BTN_* or SRC_BTN_NONE (= default)
    uint8_t  flags;                 // bit 0 = entry valid
    uint8_t  _pad[3];
} game_cfg_t;  // 50 bytes

// ── Global settings ───────────────────────────────────────────────────────────
typedef struct __attribute__((packed)) {
    uint8_t  deadzone_inner;    // 0-50  — sticks within this % of centre = centre
    uint8_t  deadzone_outer;    // 51-100 — sticks beyond this % of max = full throw
    uint8_t  trigger_threshold; // 0-255 — analogue trigger → digital D-pad cutoff
    uint8_t  rapid_fire_hz;     // 1-30 Hz
    uint16_t rapid_fire_mask;   // bitmask over DC_SLOT_* buttons
    uint8_t  invert_lx;         // bool
    uint8_t  invert_ly;         // bool
    uint8_t  invert_rx;         // bool
    uint8_t  invert_ry;         // bool
    uint8_t  default_ctrl_mode; // CTRL_MODE_* applied at boot
    uint8_t  _pad[5];           // → 16 bytes total
} global_cfg_t;

// ── Full config (stored in last flash sector) ─────────────────────────────────
#define CONFIG_MAGIC    0xDEADC0DC
#define CONFIG_VERSION  1

typedef struct __attribute__((packed)) {
    uint32_t     magic;
    uint8_t      version;
    uint8_t      _pad[3];
    global_cfg_t global;                  // 16 bytes
    game_cfg_t   games[MAX_GAME_CFGS];    // 32 × 50 = 1600 bytes
    uint32_t     crc;                     // XOR of all preceding bytes
} bt2maple_config_t;  // ≈ 1628 bytes, fits in one 4 KB flash sector

// ── Runtime current-game tracking ─────────────────────────────────────────────
// Updated by vmu_on_game_id(); read by hid_map.c.
extern uint32_t g_current_game_hash;

// ── Public API ────────────────────────────────────────────────────────────────

// Load config from flash; fills defaults if flash is blank/corrupt.
void config_store_load(void);

// Write current config to flash (erases sector, programs new data).
// Call only from Core 0 with Core 1 not running (e.g. config mode).
void config_store_save(void);

// Accessors
global_cfg_t       *config_global(void);
game_cfg_t         *config_game_by_hash(uint32_t hash);    // NULL if not found
game_cfg_t         *config_game_slot(uint8_t idx);         // idx 0..MAX_GAME_CFGS-1
game_cfg_t         *config_game_alloc(uint32_t hash);      // find or create slot
void                config_game_delete(uint32_t hash);

// Default remap table (used when game has no per-slot override).
extern const uint8_t k_default_remap[DC_NUM_BUTTONS];
