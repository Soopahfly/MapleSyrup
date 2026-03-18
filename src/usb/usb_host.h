#pragma once

// usb_host.h — TinyUSB host-mode HID controller support for bt2maple.
//
// Initialises the TinyUSB host stack and enables VBUS on the USB port.
// HID reports from wired controllers are translated to dc_controller_state_t
// via the same hid_map layer used by the Bluetooth path.
//
// Call usb_host_init() from Core 0 before the main Maple run loop.
// Call usb_host_task() periodically from Core 0's event loop.
//
// When BT2MAPLE_USB_HOST is not defined (USB host disabled), both functions
// compile to no-ops so maple.c can call them unconditionally.

#ifdef BT2MAPLE_USB_HOST

// Initialise the TinyUSB host stack and power the USB port (GPIO 24 VBUS).
void usb_host_init(void);

// Process pending USB host events.  Must be called regularly from Core 0.
void usb_host_task(void);

#else

// USB host disabled — stub out so maple.c compiles without TinyUSB.
static inline void usb_host_init(void) {}
static inline void usb_host_task(void) {}

#endif // BT2MAPLE_USB_HOST
