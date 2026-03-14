#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

// ── Transport ─────────────────────────────────────────────────────────────────
// CYW43 HCI transport is configured by pico_btstack_cyw43; nothing to add here.

// ── Protocol support ──────────────────────────────────────────────────────────
#define ENABLE_CLASSIC              // BT Classic (not BLE)
#define ENABLE_BT_LOGGER            // Route BTstack log output through stdio

// ── Profiles ─────────────────────────────────────────────────────────────────
// HID Host allows us to connect to gamepads as the host (central) device.
// SDP Client is required to discover the HID service on the gamepad.
#define ENABLE_SDP_CLIENT

// ── HCI ───────────────────────────────────────────────────────────────────────
#define HCI_ACL_PAYLOAD_SIZE        (1691 + 4)
#define HCI_INCOMING_PRE_BUFFER_SIZE 6
#define MAX_NR_HCI_CONNECTIONS      2

// ── L2CAP ─────────────────────────────────────────────────────────────────────
#define MAX_NR_L2CAP_SERVICES       3
#define MAX_NR_L2CAP_CHANNELS       6

// ── SDP ───────────────────────────────────────────────────────────────────────
#define MAX_NR_SERVICES             3
#define MAX_NR_SDAP_CONNECTIONS     1

// ── Link keys (pairing persistence) ──────────────────────────────────────────
// Stored in flash via the Pico SDK NVM helpers.
#define NVM_NUM_LINK_KEYS           4

// ── Event handlers ────────────────────────────────────────────────────────────
#define MAX_NR_BTSTACK_EVENT_HANDLERS 5

// ── Logging ───────────────────────────────────────────────────────────────────
#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR

#endif // BTSTACK_CONFIG_H
