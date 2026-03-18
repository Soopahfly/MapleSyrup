#include "config_store.h"
#include "config.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include <pb_encode.h>
#include <pb_decode.h>
#include <string.h>
#include <stdio.h>

// ── Flash addressing ──────────────────────────────────────────────────────
#define FLASH_CONFIG_OFFSET   (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define FLASH_CONFIG_ADDR     (XIP_BASE + FLASH_CONFIG_OFFSET)
#define CONFIG_MAGIC          0xDEADC0DCU
#define CONFIG_BUF_SIZE       (FLASH_SECTOR_SIZE - 8)

// ── Live config state ─────────────────────────────────────────────────────
static MapleSyrupConfig s_cfg;

// ── Runtime state ─────────────────────────────────────────────────────────
uint32_t g_current_game_hash = 0;

// ── Name tables ───────────────────────────────────────────────────────────
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

// ── Default button remap ──────────────────────────────────────────────────
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

// ── Default values ─────────────────────────────────────────────────────────
static void apply_defaults(void) {
    s_cfg.config_version = CONFIG_VERSION;
    s_cfg.has_gpio   = true;
    s_cfg.gpio       = (GpioConfig)GpioConfig_init_default;
    s_cfg.has_global = true;
    s_cfg.global     = (GlobalConfig)GlobalConfig_init_default;
    s_cfg.games_count = 0;
}

// ── Accessors ─────────────────────────────────────────────────────────────
gpio_cfg_t   *config_gpio(void)   { return &s_cfg.gpio; }
global_cfg_t *config_global(void) { return &s_cfg.global; }

game_cfg_t *config_game_by_hash(uint32_t hash) {
    if (!hash) return NULL;
    for (pb_size_t i = 0; i < s_cfg.games_count; i++) {
        if (s_cfg.games[i].hash == hash)
            return &s_cfg.games[i];
    }
    return NULL;
}

game_cfg_t *config_game_slot(uint8_t idx) {
    if (idx >= s_cfg.games_count) return NULL;
    return &s_cfg.games[idx];
}

game_cfg_t *config_game_alloc(uint32_t hash) {
    game_cfg_t *existing = config_game_by_hash(hash);
    if (existing) return existing;
    if (s_cfg.games_count >= MAX_GAME_CFGS) return NULL;
    game_cfg_t *gc = &s_cfg.games[s_cfg.games_count++];
    *gc = (GameConfig)GameConfig_init_default;
    gc->hash = hash;
    // Populate btn_remap_count so callers can iterate all 12 slots
    gc->btn_remap_count = DC_NUM_BUTTONS;
    snprintf(gc->name, sizeof(gc->name), "Game_%08lX", (unsigned long)hash);
    return gc;
}

void config_game_delete(uint32_t hash) {
    for (pb_size_t i = 0; i < s_cfg.games_count; i++) {
        if (s_cfg.games[i].hash == hash) {
            pb_size_t remaining = s_cfg.games_count - i - 1;
            if (remaining > 0)
                memmove(&s_cfg.games[i], &s_cfg.games[i + 1],
                        remaining * sizeof(GameConfig));
            s_cfg.games_count--;
            return;
        }
    }
}

// ── Persistence ───────────────────────────────────────────────────────────
void config_store_load(void) {
    apply_defaults();

    const uint8_t *flash = (const uint8_t *)FLASH_CONFIG_ADDR;

    uint32_t magic;
    memcpy(&magic, flash, sizeof(magic));
    if (magic != CONFIG_MAGIC) {
        printf("[config] Flash blank or corrupt (magic %08lX) — using defaults\n",
               (unsigned long)magic);
        return;
    }

    uint32_t pb_len;
    memcpy(&pb_len, flash + 4, sizeof(pb_len));
    if (pb_len == 0 || pb_len > CONFIG_BUF_SIZE) {
        printf("[config] Invalid payload length %lu — using defaults\n",
               (unsigned long)pb_len);
        return;
    }

    // Decode directly into s_cfg (already initialised with defaults).
    // Avoids placing a ~3 KB MapleSyrupConfig on the 2 KB Pico stack.
    pb_istream_t stream = pb_istream_from_buffer(flash + 8, pb_len);
    if (!pb_decode(&stream, MapleSyrupConfig_fields, &s_cfg)) {
        printf("[config] Decode failed: %s — using defaults\n", stream.errmsg);
        apply_defaults();
        return;
    }

    if (s_cfg.config_version != CONFIG_VERSION) {
        printf("[config] Version mismatch (stored=%u, current=%u) — using defaults\n",
               (unsigned)s_cfg.config_version, CONFIG_VERSION);
        apply_defaults();
        return;
    }

    if (!s_cfg.has_gpio)   s_cfg.gpio   = (GpioConfig)GpioConfig_init_default;
    if (!s_cfg.has_global) s_cfg.global = (GlobalConfig)GlobalConfig_init_default;
    s_cfg.has_gpio   = true;
    s_cfg.has_global = true;

    printf("[config] Loaded %u game(s) from flash\n", (unsigned)s_cfg.games_count);
}

void config_store_save(void) {
    static uint8_t encode_buf[CONFIG_BUF_SIZE];
    pb_ostream_t stream = pb_ostream_from_buffer(encode_buf, sizeof(encode_buf));

    s_cfg.config_version = CONFIG_VERSION;
    s_cfg.has_gpio   = true;
    s_cfg.has_global = true;

    if (!pb_encode(&stream, MapleSyrupConfig_fields, &s_cfg)) {
        printf("[config] Encode failed: %s\n", stream.errmsg);
        return;
    }

    uint32_t pb_len = (uint32_t)stream.bytes_written;

    static uint8_t sector_buf[FLASH_SECTOR_SIZE];
    memset(sector_buf, 0xFF, sizeof(sector_buf));

    uint32_t magic = CONFIG_MAGIC;
    memcpy(sector_buf,     &magic,      4);
    memcpy(sector_buf + 4, &pb_len,     4);
    memcpy(sector_buf + 8, encode_buf,  pb_len);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_CONFIG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_CONFIG_OFFSET, sector_buf, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);

    printf("[config] Saved %lu bytes to flash\n", (unsigned long)pb_len);
}
