#include "webconfig.h"
#include "config_store.h"
#include "controller.h"
#include "config.h"

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/watchdog.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

// ── Helpers ───────────────────────────────────────────────────────────────────
static void str_trim(char *s) {
    // Trim trailing whitespace / CRLF
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == '\r' || s[len-1] == '\n' ||
                        s[len-1] == ' '  || s[len-1] == '\t')) {
        s[--len] = '\0';
    }
    // Trim leading whitespace
    char *p = s;
    while (*p && (*p == ' ' || *p == '\t')) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

// Return pointer to first space-separated token after leading spaces.
// Advances *pos past the token and the following space.
static char *next_tok(char **pos) {
    while (**pos == ' ' || **pos == '\t') (*pos)++;
    if (!**pos) return NULL;
    char *start = *pos;
    while (**pos && **pos != ' ' && **pos != '\t') (*pos)++;
    if (**pos) { **pos = '\0'; (*pos)++; }
    return start;
}

static void print_ok(void)  { printf("OK\n"); }
static void print_err(const char *msg) { printf("ERR %s\n", msg); }

// ── Command handlers ──────────────────────────────────────────────────────────

static void cmd_help(void) {
    printf(
        "MapleSyrup Config Commands\n"
        "  help              - this message\n"
        "  get               - show all global settings\n"
        "  set KEY VALUE     - set a global setting\n"
        "    Keys: deadzone_inner, deadzone_outer, trigger_threshold,\n"
        "          rapid_fire_hz, rapid_fire_mask, invert_lx, invert_ly,\n"
        "          invert_rx, invert_ry, default_ctrl_mode\n"
        "  getpins           - show all GPIO pin assignments\n"
        "  setpin PIN VALUE  - set a GPIO pin number\n"
        "    Pins: maple_sdcka, maple_sdckb, buzzer, oled_sda, oled_scl,\n"
        "          sd_sck, sd_tx, sd_rx, sd_cs, mode_btn\n"
        "  games             - list all stored game configs\n"
        "  game HASH         - show config for game (hex hash)\n"
        "  setgame HASH KEY VALUE - set a game config value\n"
        "    Keys: name, vmu_bank (0-9 or auto), ctrl_mode (0-4 or default),\n"
        "          remap_C/B/A/START/UP/DOWN/LEFT/RIGHT/Z/Y/X/D (src button name)\n"
        "  delgame HASH      - remove game config\n"
        "  save              - write config to flash\n"
        "  reboot            - reboot into normal mode\n"
        "  srcs              - list valid source button names\n"
        "  modes             - list controller mode numbers\n"
    );
    print_ok();
}

static void cmd_get(void) {
    global_cfg_t *g = config_global();
    printf("deadzone_inner=%u\n",   g->deadzone_inner);
    printf("deadzone_outer=%u\n",   g->deadzone_outer);
    printf("trigger_threshold=%u\n",g->trigger_threshold);
    printf("rapid_fire_hz=%u\n",    g->rapid_fire_hz);
    printf("rapid_fire_mask=%u\n",  g->rapid_fire_mask);
    printf("invert_lx=%u\n",        (unsigned)g->invert_lx);
    printf("invert_ly=%u\n",        (unsigned)g->invert_ly);
    printf("invert_rx=%u\n",        (unsigned)g->invert_rx);
    printf("invert_ry=%u\n",        (unsigned)g->invert_ry);
    printf("default_ctrl_mode=%u\n",g->default_ctrl_mode);
    print_ok();
}

static void cmd_set(char *args) {
    char *key = next_tok(&args);
    char *val = next_tok(&args);
    if (!key || !val) { print_err("usage: set KEY VALUE"); return; }

    global_cfg_t *g = config_global();
    int v = atoi(val);

    if      (!strcmp(key, "deadzone_inner"))    { if(v<0||v>50)  {print_err("0-50"); return;} g->deadzone_inner=(uint32_t)v; }
    else if (!strcmp(key, "deadzone_outer"))    { if(v<51||v>100){print_err("51-100");return;} g->deadzone_outer=(uint32_t)v; }
    else if (!strcmp(key, "trigger_threshold")) { if(v<0||v>255) {print_err("0-255");return;} g->trigger_threshold=(uint32_t)v; }
    else if (!strcmp(key, "rapid_fire_hz"))     { if(v<1||v>30)  {print_err("1-30"); return;} g->rapid_fire_hz=(uint32_t)v; }
    else if (!strcmp(key, "rapid_fire_mask"))   { g->rapid_fire_mask=(uint32_t)v; }
    else if (!strcmp(key, "invert_lx"))         { g->invert_lx=(bool)(v?1:0); }
    else if (!strcmp(key, "invert_ly"))         { g->invert_ly=(bool)(v?1:0); }
    else if (!strcmp(key, "invert_rx"))         { g->invert_rx=(bool)(v?1:0); }
    else if (!strcmp(key, "invert_ry"))         { g->invert_ry=(bool)(v?1:0); }
    else if (!strcmp(key, "default_ctrl_mode")) { if(v<0||v>=(int)CTRL_MODE_COUNT){print_err("0-4");return;} g->default_ctrl_mode=(uint32_t)v; }
    else { print_err("unknown key"); return; }
    print_ok();
}

static void cmd_getpins(void) {
    gpio_cfg_t *p = config_gpio();
    printf("maple_sdcka=%d\n", p->maple_sdcka);
    printf("maple_sdckb=%d\n", p->maple_sdckb);
    printf("buzzer=%d\n",      p->buzzer);
    printf("oled_sda=%d\n",    p->oled_sda);
    printf("oled_scl=%d\n",    p->oled_scl);
    printf("sd_sck=%d\n",      p->sd_sck);
    printf("sd_tx=%d\n",       p->sd_tx);
    printf("sd_rx=%d\n",       p->sd_rx);
    printf("sd_cs=%d\n",       p->sd_cs);
    printf("mode_btn=%d\n",    p->mode_btn);
    print_ok();
}

static void cmd_setpin(char *args) {
    char *pin_name = next_tok(&args);
    char *val      = next_tok(&args);
    if (!pin_name || !val) { print_err("usage: setpin PIN VALUE"); return; }

    gpio_cfg_t *p = config_gpio();
    int v = atoi(val);
    if (v < 0 || v > 29) { print_err("pin must be 0-29"); return; }

    if      (!strcmp(pin_name, "maple_sdcka")) p->maple_sdcka = v;
    else if (!strcmp(pin_name, "maple_sdckb")) p->maple_sdckb = v;
    else if (!strcmp(pin_name, "buzzer"))      p->buzzer      = v;
    else if (!strcmp(pin_name, "oled_sda"))    p->oled_sda    = v;
    else if (!strcmp(pin_name, "oled_scl"))    p->oled_scl    = v;
    else if (!strcmp(pin_name, "sd_sck"))      p->sd_sck      = v;
    else if (!strcmp(pin_name, "sd_tx"))       p->sd_tx       = v;
    else if (!strcmp(pin_name, "sd_rx"))       p->sd_rx       = v;
    else if (!strcmp(pin_name, "sd_cs"))       p->sd_cs       = v;
    else if (!strcmp(pin_name, "mode_btn"))    p->mode_btn    = v;
    else { print_err("unknown pin name (use 'getpins' to list)"); return; }
    print_ok();
}

static void cmd_games(void) {
    int count = 0;
    int total = (int)(config_gpio() ? 0 : 0); // unused — suppress warning
    // Iterate active game slots (dense array in new protobuf model)
    for (int i = 0; i < MAX_GAME_CFGS; i++) {
        game_cfg_t *gc = config_game_slot((uint8_t)i);
        if (!gc) break;  // past end of active entries
        printf("hash=%08lX name=%s vmu_bank=%s ctrl_mode=%s\n",
               (unsigned long)gc->hash,
               gc->name,
               gc->vmu_bank == 0xFF ? "auto" : (char[4]){(char)('0'+gc->vmu_bank),0},
               gc->ctrl_mode == 0xFF ? "default" : (char[2]){(char)('0'+gc->ctrl_mode),0});
        count++;
    }
    printf("total=%d\n", count);
    print_ok();
}

static void cmd_game(char *args) {
    char *hash_str = next_tok(&args);
    if (!hash_str) { print_err("usage: game HASH"); return; }
    uint32_t hash = (uint32_t)strtoul(hash_str, NULL, 16);
    game_cfg_t *gc = config_game_by_hash(hash);
    if (!gc) { print_err("not found"); return; }

    printf("hash=%08lX\n",  (unsigned long)gc->hash);
    printf("name=%s\n",      gc->name);
    printf("vmu_bank=%s\n",  gc->vmu_bank  == 0xFF ? "auto"    : (char[4]){(char)('0'+gc->vmu_bank),0});
    printf("ctrl_mode=%s\n", gc->ctrl_mode == 0xFF ? "default" : (char[2]){(char)('0'+gc->ctrl_mode),0});
    for (int s = 0; s < DC_NUM_BUTTONS; s++) {
        uint32_t src = (s < (int)gc->btn_remap_count) ? gc->btn_remap[s] : SRC_BTN_NONE;
        printf("remap_%s=%s\n", k_dc_slot_names[s],
               src == SRC_BTN_NONE ? "default"
                                   : (src < SRC_BTN_COUNT ? k_src_btn_names[src] : "?"));
    }
    print_ok();
}

static void cmd_setgame(char *args) {
    char *hash_str = next_tok(&args);
    char *key      = next_tok(&args);
    char *val      = next_tok(&args);
    if (!hash_str || !key || !val) {
        print_err("usage: setgame HASH KEY VALUE"); return;
    }
    uint32_t hash = (uint32_t)strtoul(hash_str, NULL, 16);
    game_cfg_t *gc = config_game_alloc(hash);
    if (!gc) { print_err("no free slots"); return; }

    if (!strcmp(key, "name")) {
        strncpy(gc->name, val, sizeof(gc->name) - 1);
        gc->name[sizeof(gc->name)-1] = '\0';
    } else if (!strcmp(key, "vmu_bank")) {
        if (!strcmp(val, "auto")) gc->vmu_bank = 0xFF;
        else {
            int v = atoi(val);
            if (v < 0 || v > 9) { print_err("0-9 or auto"); return; }
            gc->vmu_bank = (uint32_t)v;
        }
    } else if (!strcmp(key, "ctrl_mode")) {
        if (!strcmp(val, "default")) gc->ctrl_mode = 0xFF;
        else {
            int v = atoi(val);
            if (v < 0 || v >= (int)CTRL_MODE_COUNT) { print_err("0-4 or default"); return; }
            gc->ctrl_mode = (uint32_t)v;
        }
    } else {
        // Check if it's a remap_SLOT key
        const char *prefix = "remap_";
        if (strncmp(key, prefix, strlen(prefix)) == 0) {
            const char *slot_name = key + strlen(prefix);
            int slot = -1;
            for (int s = 0; s < DC_NUM_BUTTONS; s++) {
                if (!strcasecmp(slot_name, k_dc_slot_names[s])) { slot = s; break; }
            }
            if (slot < 0) { print_err("unknown DC slot"); return; }

            uint32_t src = SRC_BTN_NONE;
            if (strcmp(val, "default") && strcmp(val, "none")) {
                bool found = false;
                for (int b = 0; b < SRC_BTN_COUNT; b++) {
                    if (!strcasecmp(val, k_src_btn_names[b])) {
                        src = (uint32_t)b; found = true; break;
                    }
                }
                if (!found) { print_err("unknown source button (use 'srcs' to list)"); return; }
            }
            // Ensure btn_remap_count covers this slot
            if ((pb_size_t)(slot + 1) > gc->btn_remap_count)
                gc->btn_remap_count = (pb_size_t)(slot + 1);
            gc->btn_remap[slot] = src;
        } else {
            print_err("unknown key"); return;
        }
    }
    print_ok();
}

static void cmd_delgame(char *args) {
    char *hash_str = next_tok(&args);
    if (!hash_str) { print_err("usage: delgame HASH"); return; }
    uint32_t hash = (uint32_t)strtoul(hash_str, NULL, 16);
    config_game_delete(hash);
    print_ok();
}

static void cmd_srcs(void) {
    for (int i = 0; i < SRC_BTN_COUNT; i++)
        printf("%d=%s\n", i, k_src_btn_names[i]);
    print_ok();
}

static void cmd_modes(void) {
    printf("0=STANDARD\n1=DUAL_ANALOG\n2=TWIN_STICK\n3=FIGHT_STICK\n4=RACING\n");
    print_ok();
}

// ── Main command dispatcher ───────────────────────────────────────────────────
static void dispatch(char *line) {
    if (!line || !line[0]) return;
    char *pos  = line;
    char *verb = next_tok(&pos);
    if (!verb) return;

    if      (!strcmp(verb, "help"))    cmd_help();
    else if (!strcmp(verb, "get"))     cmd_get();
    else if (!strcmp(verb, "set"))     cmd_set(pos);
    else if (!strcmp(verb, "getpins")) cmd_getpins();
    else if (!strcmp(verb, "setpin"))  cmd_setpin(pos);
    else if (!strcmp(verb, "games"))   cmd_games();
    else if (!strcmp(verb, "game"))    cmd_game(pos);
    else if (!strcmp(verb, "setgame")) cmd_setgame(pos);
    else if (!strcmp(verb, "delgame")) cmd_delgame(pos);
    else if (!strcmp(verb, "srcs"))    cmd_srcs();
    else if (!strcmp(verb, "modes"))   cmd_modes();
    else if (!strcmp(verb, "save"))    { config_store_save(); print_ok(); }
    else if (!strcmp(verb, "reboot"))  {
        printf("Rebooting...\n");
        sleep_ms(200);
        watchdog_reboot(0, 0, 0);
        for(;;) tight_loop_contents();
    }
    else { print_err("unknown command — type 'help'"); }
}

// ── Entry point ───────────────────────────────────────────────────────────────
void __attribute__((noreturn)) webconfig_run(bool sd_available) {
    // No USB serial — output goes to UART (GP0/GP1 at 115200).
    // No connection handshake needed.

    printf("\n");
    printf("========================================\n");
    printf("  MapleSyrup v" BT2MAPLE_VERSION_STRING " - Config Mode\n");
    printf("========================================\n");
    printf("SD card: %s\n", sd_available ? "mounted" : "not found");
    printf("Type 'help' for available commands.\n");
    printf("Type 'save' then 'reboot' when done.\n");
    printf("\n");
    printf("Ready>\n");

    static char line[256];
    int pos = 0;

    while (true) {
        int c = getchar_timeout_us(100000);  // 100 ms timeout, then loop
        if (c == PICO_ERROR_TIMEOUT) continue;
        if (c < 0) continue;

        if (c == '\r' || c == '\n') {
            line[pos] = '\0';
            str_trim(line);
            if (pos > 0) dispatch(line);
            pos = 0;
            printf("Ready>\n");
        } else if (c == 127 || c == '\b') {
            if (pos > 0) pos--;
        } else if (pos < (int)sizeof(line) - 1) {
            line[pos++] = (char)c;
        }
    }
}
