#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "proto/maplesyrup.pb.h"

// ── Public type aliases ────────────────────────────────────────────────────
typedef GpioConfig    gpio_cfg_t;
typedef GlobalConfig  global_cfg_t;
typedef GameConfig    game_cfg_t;

// ── Constants ─────────────────────────────────────────────────────────────
#define CONFIG_VERSION       3
#define MAX_GAME_CFGS        32
#define GAME_NAME_MAX_LEN    28

#define SRC_BTN_NONE         0xFF   // slot uses default / pass-through mapping
#define VMU_BANK_AUTO        0xFF   // auto-select based on disc hash
#define CTRL_MODE_DEFAULT    0xFF   // use global default

// ── Source button indices for remapping ───────────────────────────────────
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

// ── DC button slot indices ─────────────────────────────────────────────────
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

// ── Name tables ────────────────────────────────────────────────────────────
extern const char *const k_src_btn_names[SRC_BTN_COUNT];
extern const char *const k_dc_slot_names[DC_NUM_BUTTONS];

// ── Default button remap table ─────────────────────────────────────────────
// Used when a game has no per-slot override (SRC_BTN_NONE = use default).
extern const uint8_t k_default_remap[DC_NUM_BUTTONS];

// ── Runtime current-game tracking ─────────────────────────────────────────
// Updated by vmu_on_game_id(); read by hid_map.c.
extern uint32_t g_current_game_hash;

// ── Accessors ─────────────────────────────────────────────────────────────
gpio_cfg_t   *config_gpio(void);
global_cfg_t *config_global(void);
game_cfg_t   *config_game_by_hash(uint32_t hash);
game_cfg_t   *config_game_slot(uint8_t idx);
game_cfg_t   *config_game_alloc(uint32_t hash);
void          config_game_delete(uint32_t hash);

// ── Persistence ───────────────────────────────────────────────────────────
void config_store_load(void);
void config_store_save(void);
