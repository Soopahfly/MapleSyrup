#pragma once

// BTstack configuration for bt2maple on Pico 2 W (RP2350)
// Closely derived from the official Bluepad32 pico_w example btstack_config.h:
// https://github.com/ricardoquesada/bluepad32/blob/main/examples/pico_w/src/btstack_config.h

// ── Logging ───────────────────────────────────────────────────────────────────
#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP
#define ENABLE_SCO_OVER_HCI

// ── BLE ───────────────────────────────────────────────────────────────────────
#ifdef ENABLE_BLE
#define ENABLE_GATT_CLIENT_PAIRING
#define ENABLE_L2CAP_LE_CREDIT_BASED_FLOW_CONTROL_MODE
#define ENABLE_LE_CENTRAL
#define ENABLE_LE_DATA_LENGTH_EXTENSION
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_PRIVACY_ADDRESS_RESOLUTION
#define ENABLE_LE_SECURE_CONNECTIONS
#else
#error "BP32: ENABLE_BLE should be defined"
#endif

// ── Classic Bluetooth ─────────────────────────────────────────────────────────
#ifdef ENABLE_CLASSIC
#define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE
#define ENABLE_GOEP_L2CAP
#else
#error "BP32: ENABLE_CLASSIC should be defined"
#endif

// ── Cross-transport ───────────────────────────────────────────────────────────
#if defined(ENABLE_CLASSIC) && defined(ENABLE_BLE)
#define ENABLE_CROSS_TRANSPORT_KEY_DERIVATION
#endif

// ── Buffers and connection limits ─────────────────────────────────────────────
#define HCI_OUTGOING_PRE_BUFFER_SIZE            4   // = CYW43_PACKET_HEADER_SIZE
#define HCI_ACL_PAYLOAD_SIZE                    (1691 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT            4   // must be multiple of 4

#define MAX_NR_AVDTP_CONNECTIONS                1
#define MAX_NR_AVDTP_STREAM_ENDPOINTS           1
#define MAX_NR_AVRCP_CONNECTIONS                2
#define MAX_NR_BNEP_CHANNELS                    1
#define MAX_NR_BNEP_SERVICES                    1
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 2
#define MAX_NR_GATT_CLIENTS                     4
#define MAX_NR_HCI_CONNECTIONS                  4
#define MAX_NR_HID_HOST_CONNECTIONS             4
#define MAX_NR_HIDS_CLIENTS                     4
#define MAX_NR_HFP_CONNECTIONS                  1
#define MAX_NR_L2CAP_CHANNELS                   6
#define MAX_NR_L2CAP_SERVICES                   5
#define MAX_NR_RFCOMM_CHANNELS                  1
#define MAX_NR_RFCOMM_MULTIPLEXERS              1
#define MAX_NR_RFCOMM_SERVICES                  1
#define MAX_NR_SERVICE_RECORD_ITEMS             4
#define MAX_NR_SM_LOOKUP_ENTRIES                3
#define MAX_NR_WHITELIST_ENTRIES                16
#define MAX_NR_LE_DEVICE_DB_ENTRIES             16

// ── CYW43 shared bus — limit ACL/SCO to prevent overrun ──────────────────────
#define MAX_NR_CONTROLLER_ACL_BUFFERS           3
#define MAX_NR_CONTROLLER_SCO_PACKETS           3

// ── Flow control ─────────────────────────────────────────────────────────────
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN                 1024
#define HCI_HOST_ACL_PACKET_NUM                 3
#define HCI_HOST_SCO_PACKET_LEN                 120
#define HCI_HOST_SCO_PACKET_NUM                 3

// ── NVM / Link key storage ───────────────────────────────────────────────────
#define NVM_NUM_DEVICE_DB_ENTRIES               16
#define NVM_NUM_LINK_KEYS                       16

// ── ATT DB ───────────────────────────────────────────────────────────────────
#define MAX_ATT_DB_SIZE                         512

// ── Hardware / HAL ───────────────────────────────────────────────────────────
#define HAVE_EMBEDDED_TIME_MS
#define HAVE_ASSERT
#include <assert.h>

// ── Timing ───────────────────────────────────────────────────────────────────
// Some USB dongles take longer to respond to HCI reset (e.g. BCM20702A).
#define HCI_RESET_RESEND_TIMEOUT_MS             1000

// ── Security / pairing ───────────────────────────────────────────────────────
#define ENABLE_SOFTWARE_AES128
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS

#define HAVE_BTSTACK_STDIN
