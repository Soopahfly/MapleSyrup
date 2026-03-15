#pragma once
// TinyUSB host-mode configuration for bt2maple on Pico 2 W (RP2350).
// USB is used exclusively in host mode — stdio is routed to UART (GP0/GP1).

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// Common
//--------------------------------------------------------------------

// MCU is set by the SDK's family.cmake: OPT_MCU_RP2040 (used for both RP2040 and RP2350)
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined by the build system
#endif

// Use Pico SDK's built-in OS integration
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_PICO
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

// DMA-aligned buffers — RP2350 requires 4-byte alignment
#ifndef CFG_TUH_MEM_SECTION
#define CFG_TUH_MEM_SECTION
#endif

#ifndef CFG_TUH_MEM_ALIGN
#define CFG_TUH_MEM_ALIGN     __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// HOST mode — device mode is not used (USB is host only)
//--------------------------------------------------------------------

// Enable TinyUSB host stack
#define CFG_TUH_ENABLED       1

// Root hub port: 0 for the native RP2350 USB controller
#ifndef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT      0
#endif

// Use full-speed (USB 1.1) — matches wired gamepads
#ifndef BOARD_TUH_MAX_SPEED
#define BOARD_TUH_MAX_SPEED   OPT_MODE_FULL_SPEED
#endif

#define CFG_TUH_MAX_SPEED     BOARD_TUH_MAX_SPEED

//--------------------------------------------------------------------
// Driver configuration
//--------------------------------------------------------------------

// Descriptor enumeration scratch buffer
#define CFG_TUH_ENUMERATION_BUFSIZE 256

// Support up to 4 HID interfaces (1 controller = typically 1 interface)
#define CFG_TUH_HID                 4

// HID endpoint buffer sizes
#define CFG_TUH_HID_EPIN_BUFSIZE    64
#define CFG_TUH_HID_EPOUT_BUFSIZE   64

// Max devices (no hub needed for direct controller connection)
#define CFG_TUH_DEVICE_MAX          4

// No hub, CDC, MSC or vendor needed for gamepad use
#define CFG_TUH_HUB                 0
#define CFG_TUH_CDC                 0
#define CFG_TUH_MSC                 0
#define CFG_TUH_VENDOR              0

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
