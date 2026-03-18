// bt.c — Bluetooth backend for bt2maple using Bluepad32
//
// Bluepad32 abstracts PS3/PS4/PS5/Xbox/Switch Pro/8BitDo controllers
// through a single platform-callback API built on top of BTstack + CYW43.
//
// This file provides the uni_platform implementation and wires it into the
// project's controller_state / hid_map layer.

#include "bt.h"
#include "hid_map.h"
#include "controller.h"

#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>

// Bluepad32 master header — pulls in uni_platform, uni_hid_device, uni_gamepad, etc.
#include <uni.h>

#include <stdio.h>
#include <string.h>

// ── Forward declarations ───────────────────────────────────────────────────────
struct uni_platform* get_bt2maple_platform(void);

// ── Diagnostics ───────────────────────────────────────────────────────────────
// LED blink patterns (requires CYW43 to be initialised first):
//   2 blinks = cyw43_arch_init() succeeded
//   3 blinks = platform registered, about to call uni_init()
//   5 blinks = controller paired and ready
//   solid    = scanning for controllers

// Blink LED n times rapidly for diagnostics
static void led_blink(int n) {
    for (int i = 0; i < n; i++) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(100);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(100);
    }
    sleep_ms(500);
}

// ── Platform callbacks ─────────────────────────────────────────────────────────

static void platform_init(int argc, const char** argv) {
    (void)argc; (void)argv;
    printf("[bt] Bluepad32 platform init\n");
}

static void platform_on_init_complete(void) {
    printf("[bt] Bluepad32 init complete — starting scan\n");
    // CRITICAL: must call this or Bluepad32 never discovers controllers
    uni_bt_start_scanning_and_autoconnect_unsafe();
    printf("[bt] scanning started — LED solid\n");
    // LED solid = scanning/ready
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
}

static uni_error_t platform_on_device_discovered(bd_addr_t addr, const char* name,
                                                   uint16_t cod, uint8_t rssi) {
    (void)addr; (void)name; (void)cod; (void)rssi;
    // Accept all devices — Bluepad32 will filter to known gamepads
    return UNI_ERROR_SUCCESS;
}

static void platform_on_device_connected(uni_hid_device_t* d) {
    (void)d;
    printf("[bt] device connected\n");
}

static void platform_on_device_disconnected(uni_hid_device_t* d) {
    (void)d;
    printf("[bt] device disconnected\n");
    dc_controller_state_t neutral = DC_STATE_NEUTRAL;
    controller_state_update(&neutral);
}

static uni_error_t platform_on_device_ready(uni_hid_device_t* d) {
    (void)d;
    printf("[bt] device ready (type=%d)\n", d->controller_type);
    // 5 rapid blinks = controller paired and ready
    led_blink(5);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1); // solid = active
    return UNI_ERROR_SUCCESS;
}

static void platform_on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    (void)d;
    // Only handle gamepads
    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD) return;

    dc_controller_state_t state = DC_STATE_NEUTRAL;
    hid_map_gamepad(&ctl->gamepad, &state);
    controller_state_update(&state);
}

static const uni_property_t* platform_get_property(uni_property_idx_t idx) {
    (void)idx;
    return NULL;
}

static void platform_on_oob_event(uni_platform_oob_event_t event, void* data) {
    (void)data;
    (void)event;
}

// ── Platform descriptor ────────────────────────────────────────────────────────

static struct uni_platform s_platform = {
    .name                 = "bt2maple",
    .init                 = platform_init,
    .on_init_complete     = platform_on_init_complete,
    .on_device_discovered = platform_on_device_discovered,
    .on_device_connected  = platform_on_device_connected,
    .on_device_disconnected = platform_on_device_disconnected,
    .on_device_ready      = platform_on_device_ready,
    .on_controller_data   = platform_on_controller_data,
    .get_property         = platform_get_property,
    .on_oob_event         = platform_on_oob_event,
};

struct uni_platform* get_bt2maple_platform(void) {
    return &s_platform;
}

// ── Public API ────────────────────────────────────────────────────────────────

void bt_init(void) {
    printf("[bt] cyw43_arch_init starting\n");

    // Matches Bluepad32's official pico_w example exactly:
    // cyw43_arch_init() IS called by the app, not internally by uni_init().
    if (cyw43_arch_init()) {
        printf("[bt] cyw43_arch_init FAILED\n");
        // Hang — no LED available without CYW43
        while (1) tight_loop_contents();
    }

    // ── Stage 1: CYW43 up — 2 blinks ────────────────────────────────────────
    // If you see exactly 2 blinks, CYW43 hardware is working.
    printf("[bt] cyw43_arch_init OK — 2 blinks\n");
    led_blink(2);

    // ── Stage 2: Registering platform — 3 blinks ────────────────────────────
    printf("[bt] registering Bluepad32 platform\n");
    uni_platform_set_custom(get_bt2maple_platform());
    led_blink(3);

    // ── Stage 3: uni_init — LED off while BTstack initialises ───────────────
    // platform_on_init_complete() will turn LED solid + start scanning
    printf("[bt] calling uni_init\n");
    uni_init(0, NULL);

    printf("[bt] starting BTstack run loop\n");
    btstack_run_loop_execute(); // Never returns
}
