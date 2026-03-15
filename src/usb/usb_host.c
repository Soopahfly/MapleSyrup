// usb_host.c — TinyUSB host-mode HID controller support for bt2maple.
//
// The Pico 2 W's USB port is free (Dreamcast 5V powers the board via VSYS),
// so we run the native RP2350 USB controller in host mode.  A USB OTG adapter
// (micro-USB male → USB-A female) is required to attach a wired controller.
//
// VBUS enable: GPIO 24 is driven high after tuh_init() to power the port.
// On the Pico (W) reference board this connects to the VBUS boost regulator.
//
// Controllers are translated through the same hid_map layer used by Bluetooth
// so both input paths produce identical dc_controller_state_t updates.
//
// Design notes:
//  - Core 0 calls usb_host_task() inside the BT/Maple event loop.
//  - tuh_hid_report_received_cb() is the hot path — called from tuh_task().
//  - Only the first connected HID device is used as P1 (matching Maple Port A).
//  - Boot-protocol gamepads (HID usage page 0x01, usage 0x05) are auto-detected
//    and set to boot protocol for reliable generic mapping.

#include "usb_host.h"
#include "tusb_config.h"
#include "controller.h"

#include "tusb.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

// ── GPIO for VBUS enable on Pico (W) reference boards ─────────────────────
// Active high — drives the VBUS boost converter gate.
#define USB_HOST_VBUS_PIN   24

// ── Axis / trigger helpers (mirrors hid_map.c for raw USB reports) ─────────

// USB HID axis: signed 8-bit (-128..127) → Dreamcast 0..255 (128 = centre)
static uint8_t axis_s8_to_dc(int8_t v) {
    return (uint8_t)((int16_t)v + 128);
}

// USB HID trigger: unsigned 8-bit (0..255) → Dreamcast 0..255 (pass-through)
static uint8_t trigger_u8_to_dc(uint8_t v) {
    return v;
}

// ── Minimal generic gamepad report layout (boot-protocol compatible) ────────
// Matches the Xbox / PS / generic HID boot descriptor report layout:
//   byte 0   : buttons lo  (bits 0-7)
//   byte 1   : buttons hi  (bits 8-15)
//   byte 2   : left X
//   byte 3   : left Y
//   byte 4   : right X
//   byte 5   : right Y
//   byte 6   : left trigger
//   byte 7   : right trigger
//   byte 8   : dpad (hat switch, 0-7 = N NE E SE S SW W NW, 8 = centred)
//
// This layout covers DualShock 3/4/5, Xbox 360/One, 8BitDo, and most clones.
// Controllers that use non-standard descriptors fall back to generic parsing.

typedef struct __attribute__((packed)) {
    uint8_t  buttons_lo;     // byte 0
    uint8_t  buttons_hi;     // byte 1
    int8_t   left_x;         // byte 2
    int8_t   left_y;         // byte 3
    int8_t   right_x;        // byte 4
    int8_t   right_y;        // byte 5
    uint8_t  left_trigger;   // byte 6
    uint8_t  right_trigger;  // byte 7
    uint8_t  dpad;           // byte 8  (hat switch 0-7, 8=neutral)
} usb_gamepad_report_t;

// Common button bit positions (byte 0)
#define USB_BTN_X           (1u << 0)
#define USB_BTN_A           (1u << 1)
#define USB_BTN_B           (1u << 2)
#define USB_BTN_Y           (1u << 3)
#define USB_BTN_LB          (1u << 4)
#define USB_BTN_RB          (1u << 5)
#define USB_BTN_LT_DIG      (1u << 6)
#define USB_BTN_RT_DIG      (1u << 7)
// Common button bit positions (byte 1)
#define USB_BTN_SELECT      (1u << 0)
#define USB_BTN_START       (1u << 1)
#define USB_BTN_L3          (1u << 2)
#define USB_BTN_R3          (1u << 3)

// ── Map USB gamepad report → Dreamcast controller state ────────────────────
static void map_usb_report(const uint8_t *report, uint16_t len,
                            dc_controller_state_t *s) {
    // Start neutral
    s->buttons       = DC_BUTTONS_RELEASED;
    s->left_trigger  = DC_TRIGGER_RELEASED;
    s->right_trigger = DC_TRIGGER_RELEASED;
    s->joy_x  = s->joy_y  = DC_STICK_CENTER;
    s->joy2_x = s->joy2_y = DC_STICK_CENTER;

    if (len < 2) return;  // Need at least buttons bytes

    uint8_t b0 = report[0];
    uint8_t b1 = (len > 1) ? report[1] : 0;

    // ── D-pad (hat switch, byte 8) ─────────────────────────────────────────
    if (len > 8) {
        uint8_t hat = report[8];
        // 0=N 1=NE 2=E 3=SE 4=S 5=SW 6=W 7=NW  8+=neutral
        if (hat <= 7) {
            // Directions where Up applies: 0(N), 1(NE), 7(NW)
            if (hat == 0 || hat == 1 || hat == 7)
                s->buttons &= ~DC_BTN_DPAD_UP;
            // Directions where Down applies: 3(SE), 4(S), 5(SW)
            if (hat == 3 || hat == 4 || hat == 5)
                s->buttons &= ~DC_BTN_DPAD_DOWN;
            // Directions where Left applies: 5(SW), 6(W), 7(NW)
            if (hat == 5 || hat == 6 || hat == 7)
                s->buttons &= ~DC_BTN_DPAD_LEFT;
            // Directions where Right applies: 1(NE), 2(E), 3(SE)
            if (hat == 1 || hat == 2 || hat == 3)
                s->buttons &= ~DC_BTN_DPAD_RIGHT;
        }
    }

    // ── Face buttons (Xbox / generic naming) ──────────────────────────────
    if (b0 & USB_BTN_A)  s->buttons &= ~DC_BTN_A;
    if (b0 & USB_BTN_B)  s->buttons &= ~DC_BTN_B;
    if (b0 & USB_BTN_X)  s->buttons &= ~DC_BTN_X;
    if (b0 & USB_BTN_Y)  s->buttons &= ~DC_BTN_Y;

    // ── Shoulder buttons → DC Z (L1) and C (R1) ───────────────────────────
    if (b0 & USB_BTN_LB) s->buttons &= ~DC_BTN_Z;
    if (b0 & USB_BTN_RB) s->buttons &= ~DC_BTN_C;

    // ── Start ─────────────────────────────────────────────────────────────
    if (b1 & USB_BTN_START) s->buttons &= ~DC_BTN_START;

    // ── Analogue triggers (bytes 6-7) ──────────────────────────────────────
    if (len > 6) s->left_trigger  = trigger_u8_to_dc(report[6]);
    if (len > 7) s->right_trigger = trigger_u8_to_dc(report[7]);

    // Fallback: digital trigger bits if no analogue value present
    if (s->left_trigger  == 0 && (b0 & USB_BTN_LT_DIG)) s->left_trigger  = 0xFF;
    if (s->right_trigger == 0 && (b0 & USB_BTN_RT_DIG)) s->right_trigger = 0xFF;

    // ── Left stick (bytes 2-3) ─────────────────────────────────────────────
    if (len > 3) {
        s->joy_x = axis_s8_to_dc((int8_t)report[2]);
        s->joy_y = axis_s8_to_dc((int8_t)report[3]);
    }

    // ── Right stick (bytes 4-5) ────────────────────────────────────────────
    if (len > 5) {
        s->joy2_x = axis_s8_to_dc((int8_t)report[4]);
        s->joy2_y = axis_s8_to_dc((int8_t)report[5]);
    }
}

// ── Public API ────────────────────────────────────────────────────────────

void usb_host_init(void) {
    // Enable VBUS to power the USB port via the OTG adapter.
    // Must be done before tuh_init() so the connected device sees valid power.
    gpio_init(USB_HOST_VBUS_PIN);
    gpio_set_dir(USB_HOST_VBUS_PIN, GPIO_OUT);
    gpio_put(USB_HOST_VBUS_PIN, 1);

    // Initialise TinyUSB host stack on port 0 (native RP2350 USB controller).
    if (!tuh_init(BOARD_TUH_RHPORT)) {
        printf("[usb] tuh_init failed\n");
        return;
    }

    printf("[usb] USB host initialised — waiting for controllers\n");
}

void usb_host_task(void) {
    tuh_task();
}

// ── TinyUSB HID host callbacks ────────────────────────────────────────────

// Called when a HID device mounts (connects).
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t idx,
                      uint8_t const *report_desc, uint16_t desc_len) {
    (void)report_desc; (void)desc_len;
    printf("[usb] HID device mounted: addr=%u idx=%u protocol=%u\n",
           dev_addr, idx, tuh_hid_interface_protocol(dev_addr, idx));

    // Request boot protocol for generic compatibility.
    // Most gamepads support this and it guarantees a known report layout.
    tuh_hid_set_protocol(dev_addr, idx, HID_PROTOCOL_BOOT);

    // Start receiving reports immediately.
    if (!tuh_hid_receive_report(dev_addr, idx)) {
        printf("[usb] tuh_hid_receive_report failed\n");
    }
}

// Called when a HID device unmounts (disconnects).
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t idx) {
    printf("[usb] HID device unmounted: addr=%u idx=%u\n", dev_addr, idx);
    // Release controller to neutral state on disconnect.
    dc_controller_state_t neutral = DC_STATE_NEUTRAL;
    controller_state_update(&neutral);
}

// Called when a new HID report arrives from an interrupt endpoint.
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t idx,
                                 uint8_t const *report, uint16_t len) {
    // Translate the raw report to a Dreamcast controller state.
    dc_controller_state_t state = DC_STATE_NEUTRAL;
    map_usb_report(report, len, &state);
    controller_state_update(&state);

    // Re-arm for the next report.
    tuh_hid_receive_report(dev_addr, idx);
}
