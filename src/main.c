#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "controller.h"
#include "maple/maple.h"
#include "maple/vmu.h"
#include "bluetooth/bt.h"
#include "storage/sd_card.h"
#include "usb/usb_host.h"

// Core 1 entry point: runs the BTstack + Bluepad32 event loop.
// Never returns.
static void core1_entry(void) {
    bt_init();
    bt_run();
}

int main(void) {
    stdio_init_all();

    // Initialise shared controller state before either core accesses it.
    controller_state_init();

    // Initialise USB host stack (TinyUSB host mode, VBUS enabled on GPIO 24).
    // Must be called before launching Core 1 so TinyUSB is ready before any
    // async_context work begins.  The task is polled from the maple_run loop.
    usb_host_init();

    // Core 1 owns Bluetooth — launch it first so pairing can begin
    // while Core 0 waits for Maple frames.
    multicore_launch_core1(core1_entry);

    // Initialise SD card early (before maple_init which calls vmu_init).
    bool sd_ok = sd_init();

    // maple_init() calls vmu_init() which sets up the in-RAM VMU image (0xFF).
    // Core 0 owns the Maple bus.
    maple_init();

    // Attach SD card storage: loads vmu_a.bin or creates a blank image.
    // Must be called after maple_init() / vmu_init().
    vmu_sd_attach(sd_ok);

    // maple_run() is the Core 0 main loop — it tight-polls the PIO RX FIFO.
    // usb_host_task() is called from within that loop so TinyUSB events are
    // serviced without a separate thread.
    maple_run();   // Never returns
}
