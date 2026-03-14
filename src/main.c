#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "controller.h"
#include "maple/maple.h"
#include "bluetooth/bt.h"

// Core 1 entry point: runs the BTstack event loop.
// Never returns.
static void core1_entry(void) {
    bt_init();
    bt_run();
}

int main(void) {
    stdio_init_all();

    // Initialise shared controller state before either core accesses it.
    controller_state_init();

    // Core 1 owns Bluetooth — launch it first so pairing can begin
    // while Core 0 waits for Maple frames.
    multicore_launch_core1(core1_entry);

    // Core 0 owns the Maple bus.
    maple_init();
    maple_run();   // Never returns — tight-polls PIO RX FIFO
}
