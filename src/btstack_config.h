#pragma once

// BTstack configuration for bt2maple on Pico 2 W (RP2350)
// Derived from the Bluepad32 pico_w example btstack_config.h

// ── Logging ───────────────────────────────────────────────────────────────────
#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR
// #define ENABLE_LOG_DEBUG
#define ENABLE_PRINTF_HEXDUMP

// ── Assert handling ───────────────────────────────────────────────────────────
// Tell btstack_debug.h to use libc assert (which NDEBUG suppresses).
// This also ensures <assert.h> is pulled in via btstack_debug.h for any
// Bluepad32 parser that calls assert() without including assert.h itself.
#define HAVE_ASSERT
#include <assert.h>    // force assert.h into every TU that includes this file

// ── Classic Bluetooth ─────────────────────────────────────────────────────────
#ifndef ENABLE_CLASSIC
#define ENABLE_CLASSIC
#endif
#define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE
#define ENABLE_HID_HOST

// ── BLE ───────────────────────────────────────────────────────────────────────
#ifndef ENABLE_BLE
#define ENABLE_BLE
#endif
#define ENABLE_LE_CENTRAL
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_SECURE_CONNECTIONS
#define ENABLE_LE_PRIVACY_ADDRESS_RESOLUTION
#define ENABLE_GATT_CLIENT_OVER_CLASSIC

// When both Classic and BLE are enabled
#define ENABLE_CROSS_TRANSPORT_KEY_DERIVATION

// ── Secure Simple Pairing ────────────────────────────────────────────────────
#define ENABLE_SOFTWARE_AES128
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS

// ── ATT DB ───────────────────────────────────────────────────────────────────
// att_db_util.c requires either HAVE_MALLOC or MAX_ATT_DB_SIZE
#define MAX_ATT_DB_SIZE                         512

// ── Buffers and connection limits ────────────────────────────────────────────
// Keep conservative — shared CYW43 HCI bus can't handle deep queues.
#define HCI_ACL_PAYLOAD_SIZE                    (1691 + 4)
#define MAX_NR_HCI_CONNECTIONS                  4
#define MAX_NR_L2CAP_CHANNELS                   6
#define MAX_NR_L2CAP_SERVICES                   3
#define MAX_NR_GATT_CLIENTS                     4
#define MAX_NR_SM_LOOKUP_ENTRIES                3
#define MAX_NR_WHITELIST_ENTRIES                4
#define MAX_NR_LE_DEVICE_DB_ENTRIES             4
#define MAX_NR_CONTROLLER_ACL_BUFFERS           3

// ── Flow control ─────────────────────────────────────────────────────────────
// Needed to prevent overruns on the shared CYW43 bus
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN                 1024
#define HCI_HOST_ACL_PACKET_NUM                 3
#define HCI_HOST_SCO_PACKET_LEN                 255
#define HCI_HOST_SCO_PACKET_NUM                 3

// ── NVM / Link key storage ───────────────────────────────────────────────────
#define NVM_NUM_DEVICE_DB_ENTRIES               16
#define NVM_NUM_LINK_KEYS                       16

// ── CYW43 transport requirements ─────────────────────────────────────────────
// Required by btstack_hci_transport_cyw43.c
#define HCI_OUTGOING_PRE_BUFFER_SIZE            4   // = CYW43_PACKET_HEADER_SIZE
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT            4   // must be multiple of 4

// ── Timing / hardware ────────────────────────────────────────────────────────
// 1000 ms gives slow USB dongles time to respond to HCI_Reset
#define HCI_RESET_RESEND_TIMEOUT_MS             1000
#define HAVE_EMBEDDED_TIME_MS
