/* Automatically generated nanopb source (nanopb 0.3.x compatible)
 * Regenerate: python nanopb_generator.py src/proto/maplesyrup.proto
 */
#include "maplesyrup.pb.h"

/* ── GpioConfig ───────────────────────────────────────────────────────────── */
const pb_field_t GpioConfig_fields[11] = {
    PB_FIELD(  1, INT32,    SINGULAR, STATIC, FIRST, GpioConfig, maple_sdcka, maple_sdcka, 0),
    PB_FIELD(  2, INT32,    SINGULAR, STATIC, OTHER, GpioConfig, maple_sdckb, maple_sdcka, 0),
    PB_FIELD(  3, INT32,    SINGULAR, STATIC, OTHER, GpioConfig, buzzer,      maple_sdckb, 0),
    PB_FIELD(  4, INT32,    SINGULAR, STATIC, OTHER, GpioConfig, oled_sda,    buzzer,      0),
    PB_FIELD(  5, INT32,    SINGULAR, STATIC, OTHER, GpioConfig, oled_scl,    oled_sda,    0),
    PB_FIELD(  6, INT32,    SINGULAR, STATIC, OTHER, GpioConfig, sd_sck,      oled_scl,    0),
    PB_FIELD(  7, INT32,    SINGULAR, STATIC, OTHER, GpioConfig, sd_tx,       sd_sck,      0),
    PB_FIELD(  8, INT32,    SINGULAR, STATIC, OTHER, GpioConfig, sd_rx,       sd_tx,       0),
    PB_FIELD(  9, INT32,    SINGULAR, STATIC, OTHER, GpioConfig, sd_cs,       sd_rx,       0),
    PB_FIELD( 10, INT32,    SINGULAR, STATIC, OTHER, GpioConfig, mode_btn,    sd_cs,       0),
    PB_LAST_FIELD
};

/* ── GlobalConfig ─────────────────────────────────────────────────────────── */
const pb_field_t GlobalConfig_fields[11] = {
    PB_FIELD(  1, UINT32,   SINGULAR, STATIC, FIRST, GlobalConfig, deadzone_inner,    deadzone_inner,    0),
    PB_FIELD(  2, UINT32,   SINGULAR, STATIC, OTHER, GlobalConfig, deadzone_outer,    deadzone_inner,    0),
    PB_FIELD(  3, UINT32,   SINGULAR, STATIC, OTHER, GlobalConfig, trigger_threshold, deadzone_outer,    0),
    PB_FIELD(  4, UINT32,   SINGULAR, STATIC, OTHER, GlobalConfig, rapid_fire_hz,     trigger_threshold, 0),
    PB_FIELD(  5, UINT32,   SINGULAR, STATIC, OTHER, GlobalConfig, rapid_fire_mask,   rapid_fire_hz,     0),
    PB_FIELD(  6, BOOL,     SINGULAR, STATIC, OTHER, GlobalConfig, invert_lx,         rapid_fire_mask,   0),
    PB_FIELD(  7, BOOL,     SINGULAR, STATIC, OTHER, GlobalConfig, invert_ly,         invert_lx,         0),
    PB_FIELD(  8, BOOL,     SINGULAR, STATIC, OTHER, GlobalConfig, invert_rx,         invert_ly,         0),
    PB_FIELD(  9, BOOL,     SINGULAR, STATIC, OTHER, GlobalConfig, invert_ry,         invert_rx,         0),
    PB_FIELD( 10, UINT32,   SINGULAR, STATIC, OTHER, GlobalConfig, default_ctrl_mode, invert_ry,         0),
    PB_LAST_FIELD
};

/* ── GameConfig ───────────────────────────────────────────────────────────── */
const pb_field_t GameConfig_fields[6] = {
    PB_FIELD(  1, UINT32,   SINGULAR, STATIC, FIRST, GameConfig, hash,      hash,     0),
    PB_FIELD(  2, STRING,   SINGULAR, STATIC, OTHER, GameConfig, name,      hash,     0),
    PB_FIELD(  3, UINT32,   SINGULAR, STATIC, OTHER, GameConfig, vmu_bank,  name,     0),
    PB_FIELD(  4, UINT32,   SINGULAR, STATIC, OTHER, GameConfig, ctrl_mode, vmu_bank, 0),
    PB_FIELD(  5, UINT32,   REPEATED, STATIC, OTHER, GameConfig, btn_remap, ctrl_mode,0),
    PB_LAST_FIELD
};

/* ── MapleSyrupConfig ─────────────────────────────────────────────────────── */
const pb_field_t MapleSyrupConfig_fields[5] = {
    PB_FIELD(  1, UINT32,   SINGULAR, STATIC,   FIRST, MapleSyrupConfig, config_version, config_version, 0),
    PB_FIELD(  2, MESSAGE,  OPTIONAL, STATIC,   OTHER, MapleSyrupConfig, gpio,           config_version, &GpioConfig_fields),
    PB_FIELD(  3, MESSAGE,  OPTIONAL, STATIC,   OTHER, MapleSyrupConfig, global,         gpio,           &GlobalConfig_fields),
    PB_FIELD(  4, MESSAGE,  REPEATED, STATIC,   OTHER, MapleSyrupConfig, games,          global,         &GameConfig_fields),
    PB_LAST_FIELD
};
