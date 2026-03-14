#pragma once

// Initialise the CYW43 chip and BTstack.
// Sets the device to page-scan mode so gamepads can connect to it.
// Call from Core 1 before bt_run().
void bt_init(void);

// BTstack event loop.  Processes HID reports and calls controller_state_update().
// Runs on Core 1.  Never returns.
void bt_run(void);
