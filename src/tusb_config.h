#pragma once
// TinyUSB device-mode configuration for bt2maple on Pico 2 W (RP2350).
//
// USB is in CDC device mode — used for the web config console (pico_stdio_usb).
// Debug output also goes to UART on GP0/GP1.
//
// NOTE: TinyUSB cannot simultaneously be in host and device mode on one USB
// controller.  Wired USB controller host support is reserved for a future
// release (will require hardware changes or a second USB controller).

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

// MCU supplied by SDK family.cmake
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined by the build system
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS     OPT_OS_PICO
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG  0
#endif

// ── Device mode ───────────────────────────────────────────────────────────────
// RHPORT0 = RP2350 native USB hardware, device mode, full speed
#define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUD_ENABLED         1

// CDC virtual serial port (used by pico_stdio_usb / config console)
#define CFG_TUD_CDC             1
#define CFG_TUD_CDC_RX_BUFSIZE  512
#define CFG_TUD_CDC_TX_BUFSIZE  512

// Unused device classes
#define CFG_TUD_MSC             0
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

// ── Host mode (disabled) ──────────────────────────────────────────────────────
#define CFG_TUH_ENABLED         0

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
