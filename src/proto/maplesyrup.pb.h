/* Automatically generated nanopb header (nanopb 0.3.x compatible)
 * Regenerate: python nanopb_generator.py src/proto/maplesyrup.proto
 * These pre-generated files are committed so the build works without Python.
 */
#ifndef PB_MAPLESYRUP_PB_H_INCLUDED
#define PB_MAPLESYRUP_PB_H_INCLUDED
#include <pb.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Struct definitions */

typedef struct _GpioConfig {
    int32_t maple_sdcka;
    int32_t maple_sdckb;
    int32_t buzzer;
    int32_t oled_sda;
    int32_t oled_scl;
    int32_t sd_sck;
    int32_t sd_tx;
    int32_t sd_rx;
    int32_t sd_cs;
    int32_t mode_btn;
} GpioConfig;

typedef struct _GlobalConfig {
    uint32_t deadzone_inner;
    uint32_t deadzone_outer;
    uint32_t trigger_threshold;
    uint32_t rapid_fire_hz;
    uint32_t rapid_fire_mask;
    bool invert_lx;
    bool invert_ly;
    bool invert_rx;
    bool invert_ry;
    uint32_t default_ctrl_mode;
} GlobalConfig;

typedef struct _GameConfig {
    uint32_t hash;
    char name[29];
    uint32_t vmu_bank;
    uint32_t ctrl_mode;
    pb_size_t btn_remap_count;
    uint32_t btn_remap[12];
} GameConfig;

typedef struct _MapleSyrupConfig {
    uint32_t config_version;
    bool has_gpio;
    GpioConfig gpio;
    bool has_global;
    GlobalConfig global;
    pb_size_t games_count;
    GameConfig games[32];
} MapleSyrupConfig;

/* Default-value initializers */
#define GpioConfig_init_default    {16, 17, 3, 4, 5, 10, 11, 12, 13, 22}
#define GlobalConfig_init_default  {10, 90, 30, 10, 0, false, false, false, false, 0}
#define GameConfig_init_default    {0, "", 0xFF, 0xFF, 0, {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}}
#define MapleSyrupConfig_init_default {2, false, GpioConfig_init_default, false, GlobalConfig_init_default, 0, {}}

/* Field descriptor arrays (nanopb 0.3.x: extern const pb_field_t[]) */
extern const pb_field_t GpioConfig_fields[11];
extern const pb_field_t GlobalConfig_fields[11];
extern const pb_field_t GameConfig_fields[6];
extern const pb_field_t MapleSyrupConfig_fields[5];

/* Maximum encoded sizes */
#define GpioConfig_size          55
#define GlobalConfig_size        50
#define GameConfig_size          85
#define MapleSyrupConfig_size    (15 + GpioConfig_size + GlobalConfig_size + 32 * (2 + GameConfig_size))

#ifdef __cplusplus
}
#endif

#endif /* PB_MAPLESYRUP_PB_H_INCLUDED */
