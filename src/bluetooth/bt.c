#include "bt.h"
#include "hid_map.h"
#include "controller.h"

#include "pico/cyw43_arch.h"
#include "btstack.h"
#include "btstack_hid_parser.h"

#include <stdio.h>
#include <string.h>

// ── BTstack component instances ───────────────────────────────────────────────
static btstack_packet_callback_registration_t hci_event_cb;

// HID host descriptor storage — enough for one device at a time.
// Increase if supporting simultaneous connections.
#define HID_DESCRIPTOR_MAX 1024
static uint8_t hid_descriptor_storage[HID_DESCRIPTOR_MAX];

// ── Rumble forwarding ─────────────────────────────────────────────────────────
// Checked in the BTstack run loop; if non-zero, a rumble command is pending.
// Only DS4/DS5 and Xbox support rumble over BT.
static hid_con_handle_t g_hid_handle = HID_INVALID_CON_HANDLE;
static uint8_t g_last_rumble = 0;

static void send_rumble_ds4(uint8_t intensity) {
    if (g_hid_handle == HID_INVALID_CON_HANDLE) return;
    // DS4 output report 0x11 via BT HID control channel
    uint8_t report[] = {
        0x52,           // SET_REPORT (output)
        0x11,           // report ID
        0xC0, 0x20,     // flags
        0x00,           // rumble right (weak)
        intensity,      // rumble left  (strong)
        0x00, 0x00, 0x00, // LED RGB (leave unchanged)
        0x00, 0x00,
    };
    hid_host_send_output_report(g_hid_handle, report, sizeof(report));
}

// ── HID Report Handler ────────────────────────────────────────────────────────
static void hid_host_packet_handler(uint8_t packet_type, uint16_t channel,
                                     uint8_t *packet, uint16_t size) {
    (void)channel;

    if (packet_type == HCI_EVENT_PACKET) {
        switch (hci_event_packet_get_type(packet)) {

            case HCI_EVENT_HID_META: {
                uint8_t sub = hci_event_hid_meta_get_subevent_code(packet);
                switch (sub) {
                    case HID_SUBEVENT_CONNECTION_OPENED: {
                        uint8_t status = hid_subevent_connection_opened_get_status(packet);
                        if (status != ERROR_CODE_SUCCESS) {
                            printf("[bt] HID open failed: 0x%02X\n", status);
                            break;
                        }
                        g_hid_handle = hid_subevent_connection_opened_get_con_handle(packet);
                        printf("[bt] HID connected (handle 0x%04X)\n", g_hid_handle);
                        break;
                    }
                    case HID_SUBEVENT_CONNECTION_CLOSED:
                        printf("[bt] HID disconnected\n");
                        g_hid_handle = HID_INVALID_CON_HANDLE;
                        g_last_rumble = 0;
                        {
                            dc_controller_state_t neutral = DC_STATE_NEUTRAL;
                            controller_state_update(&neutral);
                        }
                        break;
                    case HID_SUBEVENT_INPUT_REPORT: {
                        const uint8_t *report = hid_subevent_input_report_get_report(packet);
                        uint16_t len = hid_subevent_input_report_get_report_len(packet);
                        dc_controller_state_t state = DC_STATE_NEUTRAL;
                        hid_map_report(report, len, &state);
                        controller_state_update(&state);
                        break;
                    }
                    default:
                        break;
                }
                break;
            }

            default:
                break;
        }
        return;
    }
}

// ── HCI Event Handler ─────────────────────────────────────────────────────────
static void hci_event_handler(uint8_t packet_type, uint16_t channel,
                               uint8_t *packet, uint16_t size) {
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                printf("[bt] HCI ready — scannable, waiting for gamepad\n");
                // Make us discoverable and connectable so a gamepad can pair.
                gap_set_default_link_policy_settings(
                    LM_LINK_POLICY_ENABLE_SNIFF_MODE |
                    LM_LINK_POLICY_ENABLE_ROLE_SWITCH);
                gap_discoverable_control(1);
                gap_connectable_control(1);
                // Require SSP (Secure Simple Pairing) — most modern controllers use it.
                gap_set_security_level(LEVEL_2);
            }
            break;

        case HCI_EVENT_PIN_CODE_REQUEST:
            // Legacy pairing: accept "0000" for older devices.
            hci_event_pin_code_request_get_bd_addr(packet, (bd_addr_t){0});
            hci_send_cmd(&hci_pin_code_request_reply,
                         hci_event_pin_code_request_get_bd_addr(packet),
                         4, "0000");
            break;

        case HCI_EVENT_USER_CONFIRMATION_REQUEST:
            // SSP: auto-confirm — fine for a dedicated controller adapter.
            hci_send_cmd(&hci_user_confirmation_request_reply,
                         hci_event_user_confirmation_request_get_bd_addr(packet));
            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE: {
            printf("[bt] disconnected\n");
            dc_controller_state_t neutral = DC_STATE_NEUTRAL;
            controller_state_update(&neutral);
            break;
        }

        default:
            break;
    }
}

// ── Rumble poll (called once per BTstack run-loop tick via a timer) ───────────
static btstack_timer_source_t rumble_timer;

static void rumble_timer_handler(btstack_timer_source_t *ts) {
    uint8_t intensity = g_rumble_intensity;
    if (intensity != g_last_rumble) {
        g_last_rumble = intensity;
        send_rumble_ds4(intensity);
    }
    btstack_run_loop_set_timer(ts, 16);   // re-arm every ~16 ms
    btstack_run_loop_add_timer(ts);
}

// ── Init ──────────────────────────────────────────────────────────────────────
void bt_init(void) {
    if (cyw43_arch_init()) {
        printf("[bt] cyw43_arch_init failed!\n");
        return;
    }

    // HCI events (connection management, SSP)
    hci_event_cb.callback = hci_event_handler;
    hci_add_event_handler(&hci_event_cb);

    // HID Host profile
    hid_host_init(hid_descriptor_storage, sizeof(hid_descriptor_storage));
    hid_host_register_packet_handler(hid_host_packet_handler);

    // Rumble polling timer
    btstack_run_loop_set_timer_handler(&rumble_timer, rumble_timer_handler);
    btstack_run_loop_set_timer(&rumble_timer, 100);
    btstack_run_loop_add_timer(&rumble_timer);

    hci_power_control(HCI_POWER_ON);
    printf("[bt] init complete\n");
}

// ── Run ───────────────────────────────────────────────────────────────────────
void bt_run(void) {
    btstack_run_loop_execute();
}
