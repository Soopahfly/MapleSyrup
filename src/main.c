#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "controller.h"
#include "maple/maple.h"
#include "maple/vmu.h"
#include "bluetooth/bt.h"
#include "storage/sd_card.h"
#include "display/oled.h"

int main(void) {
    stdio_init_all();
    sleep_ms(500); // Let UART settle

    printf("\n\nbt2maple v" BT2MAPLE_VERSION_STRING "\n");
    printf("[main] Core 0 starting\n");

    // ── 1. Controller state (shared between cores via spinlock) ──────────────
    controller_state_init();

    // ── 2. Maple bus PIO + VMU init ──────────────────────────────────────────
    // maple_init() calls vmu_init() internally.
    maple_init();
    oled_init();

    // ── 3. SD card (optional — VMU works from RAM if absent) ─────────────────
    bool sd_ok = sd_init();
    vmu_sd_attach(sd_ok);   // loads VMU image from SD if available

    // ── 4. Bluetooth on Core 1 ───────────────────────────────────────────────
    // bt_init() never returns — it calls btstack_run_loop_execute().
    // Core 1 handles all BT/Bluepad32 work; Core 0 stays in the Maple loop.
    printf("[main] launching BT on Core 1\n");
    multicore_launch_core1(bt_init);

    // ── 5. Maple bus tight-poll loop on Core 0 (never returns) ───────────────
    printf("[main] entering Maple run loop on Core 0\n");
    maple_run();
}
