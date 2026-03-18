#include "config_store.h"
#include <string.h>
#include <stdio.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"

// ── Flash location ────────────────────────────────────────────────────────────
// Last 4 KB sector of flash.  PICO_FLASH_SIZE_BYTES is set by the board file
// (4 MB for Pico 2 W).  XIP_BASE is 0x10000000.
#define FLASH_CONFIG_OFFSET  (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

// ── Name tables ───────────────────────────────────────────────────────────────
const char *const k_src_btn_names[SRC_BTN_COUNT] = {
    "A", "B", "X", "Y",
    "L_SHOULDER", "R_SHOULDER",
    "L_TRIGGER", "R_TRIGGER",
    "SELECT", "START",
    "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT",
};

const char *const k_dc_slot_names[DC_NUM_BUTTONS] = {
    "C", "B", "A", "START",
    "UP", "DOWN", "LEFT", "RIGHT",
    "Z", "Y", "X", "D",
};

// ── Default button remap ──────────────────────────────────────────────────────
const uint8_t k_default_remap[DC_NUM_BUTTONS] = {
    SRC_BTN_R_SHOULDER,  // C
    SRC_BTN_B,           // B
    SRC_BTN_A,           // A
    SRC_BTN_START,       // Start
    SRC_DPAD_UP,         // Up
    SRC_DPAD_DOWN,       // Down
    SRC_DPAD_LEFT,       // Left
    SRC_DPAD_RIGHT,      // Right
    SRC_BTN_L_SHOULDER,  // Z
    SRC_BTN_Y,           // Y
    SRC_BTN_X,           // X
    SRC_BTN_NONE,        // D  (no default)
};

// ── Runtime state ─────────────────────────────────────────────────────────────
static bt2maple_config_t g_config;
uint32_t g_current_game_hash = 0;

// ── CRC helper ────────────────────────────────────────────────────────────────
static uint32_t compute_crc(const bt2maple_config_t *cfg) {
    const uint8_t *p = (const uint8_t *)cfg;
    uint32_t crc = 0;
    // XOR all bytes except the trailing crc field itself
    size_t len = sizeof(*cfg) - sizeof(cfg->crc);
    for (size_t i = 0; i < len; i++) crc ^= p[i];
    return crc;
}

// ── Defaults ──────────────────────────────────────────────────────────────────
static void apply_defaults(bt2maple_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->magic   = CONFIG_MAGIC;
    cfg->version = CONFIG_VERSION;

    global_cfg_t *g = &cfg->global;
    g->deadzone_inner    = 10;   // 10% inner dead zone
    g->deadzone_outer    = 95;   // 95% outer saturation
    g->trigger_threshold = 30;   // ~12% of full scale
    g->rapid_fire_hz     = 10;
    g->rapid_fire_mask   = 0;
    g->invert_lx         = 0;
    g->invert_ly         = 0;
    g->invert_rx         = 0;
    g->invert_ry         = 0;
    g->default_ctrl_mode = 0;    // CTRL_MODE_STANDARD

    // All game slots empty
    for (int i = 0; i < MAX_GAME_CFGS; i++) {
        cfg->games[i].hash = 0;
        cfg->games[i].flags = 0;
        cfg->games[i].vmu_bank   = 0xFF;  // auto
        cfg->games[i].ctrl_mode  = 0xFF;  // global default
        memset(cfg->games[i].btn_remap, SRC_BTN_NONE, DC_NUM_BUTTONS);
    }
    cfg->crc = compute_crc(cfg);
}

// ── Public API ────────────────────────────────────────────────────────────────
void config_store_load(void) {
    const bt2maple_config_t *stored =
        (const bt2maple_config_t *)(XIP_BASE + FLASH_CONFIG_OFFSET);

    if (stored->magic   == CONFIG_MAGIC &&
        stored->version == CONFIG_VERSION &&
        stored->crc     == compute_crc(stored)) {
        memcpy(&g_config, stored, sizeof(g_config));
        printf("[cfg] loaded from flash\n");
    } else {
        apply_defaults(&g_config);
        printf("[cfg] using defaults (flash blank or corrupt)\n");
    }
}

void config_store_save(void) {
    g_config.magic   = CONFIG_MAGIC;
    g_config.version = CONFIG_VERSION;
    g_config.crc     = compute_crc(&g_config);

    // Prepare a sector-sized buffer (4 KB), copy config, pad rest with 0xFF.
    static uint8_t buf[FLASH_SECTOR_SIZE];
    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, &g_config, sizeof(g_config));

    // Erase + program.  Interrupts must be disabled; Core 1 must not be running.
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_CONFIG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_CONFIG_OFFSET, buf, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    printf("[cfg] saved to flash\n");
}

global_cfg_t *config_global(void) {
    return &g_config.global;
}

game_cfg_t *config_game_by_hash(uint32_t hash) {
    if (!hash) return NULL;
    for (int i = 0; i < MAX_GAME_CFGS; i++) {
        if ((g_config.games[i].flags & 1) && g_config.games[i].hash == hash)
            return &g_config.games[i];
    }
    return NULL;
}

game_cfg_t *config_game_slot(uint8_t idx) {
    if (idx >= MAX_GAME_CFGS) return NULL;
    return &g_config.games[idx];
}

game_cfg_t *config_game_alloc(uint32_t hash) {
    // Existing entry?
    game_cfg_t *existing = config_game_by_hash(hash);
    if (existing) return existing;

    // Find a free slot
    for (int i = 0; i < MAX_GAME_CFGS; i++) {
        if (!(g_config.games[i].flags & 1)) {
            memset(&g_config.games[i], 0, sizeof(game_cfg_t));
            g_config.games[i].hash      = hash;
            g_config.games[i].flags     = 1;
            g_config.games[i].vmu_bank  = 0xFF;
            g_config.games[i].ctrl_mode = 0xFF;
            memset(g_config.games[i].btn_remap, SRC_BTN_NONE, DC_NUM_BUTTONS);
            snprintf(g_config.games[i].name, sizeof(g_config.games[i].name),
                     "Game_%08lX", (unsigned long)hash);
            return &g_config.games[i];
        }
    }
    return NULL;  // all slots full
}

void config_game_delete(uint32_t hash) {
    for (int i = 0; i < MAX_GAME_CFGS; i++) {
        if ((g_config.games[i].flags & 1) && g_config.games[i].hash == hash) {
            memset(&g_config.games[i], 0, sizeof(game_cfg_t));
            return;
        }
    }
}
