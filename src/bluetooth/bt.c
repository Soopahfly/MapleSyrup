#include "bt.h"
#include "hid_map.h"
#include "controller.h"

#include "pico/cyw43_arch.h"
#include "btstack.h"

#include <stdio.h>
#include <string.h>

// ── State ─────────────────────────────────────────────────────────────────────
static btstack_packet_callback_registration_t hci_event_callback_registration;

// ── HID Report Handler ────────────────────────────────────────────────────────
// Called by BTstack on every incoming HID interrupt-channel packet.
// Runs on Core 1 inside the BTstack event loop — zero extra latency.
static void hid_report_handler(uint8_t packet_type, uint16_t channel,
                                uint8_t *packet, uint16_t size) {
    if (packet_type != HID_SUBEVENT_INPUT_REPORT) return;

    dc_controller_state_t state;
    hid_map_report(packet, size, &state);
    controller_state_update(&state);
}

// ── HCI / Connection Event Handler ───────────────────────────────────────────
static void hci_event_handler(uint8_t packet_type, uint16_t channel,
                               uint8_t *packet, uint16_t size) {
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                printf("[bt] HCI ready — waiting for gamepad connection\n");
                // TODO: If a paired device address is stored in flash (NVM link
                //       key), initiate outbound connection here instead of waiting
                //       for the gamepad to connect to us.
            }
            break;

        case HCI_EVENT_CONNECTION_COMPLETE:
            printf("[bt] Connection complete\n");
            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE:
            printf("[bt] Disconnected — returning to scan\n");
            // Reset controller to neutral so the DC doesn't see stuck inputs.
            {
                dc_controller_state_t neutral = {
                    .buttons       = DC_BUTTONS_RELEASED,
                    .right_trigger = DC_TRIGGER_RELEASED,
                    .left_trigger  = DC_TRIGGER_RELEASED,
                    .joy_x         = DC_STICK_CENTER,
                    .joy_y         = DC_STICK_CENTER,
                    .joy2_x        = DC_STICK_CENTER,
                    .joy2_y        = DC_STICK_CENTER,
                };
                controller_state_update(&neutral);
            }
            break;

        default:
            break;
    }
}

// ── Init ──────────────────────────────────────────────────────────────────────
void bt_init(void) {
    if (cyw43_arch_init()) {
        printf("[bt] cyw43_arch_init failed!\n");
        return;
    }

    // Register HCI event handler (connection management)
    hci_event_callback_registration.callback = hci_event_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    // TODO: Register HID host profile callbacks.
    //
    // The Pico SDK bundles BTstack; the HID host API surface depends on the
    // exact version shipped with your SDK release.  Typical setup:
    //
    //   hid_host_init(hid_descriptor_storage, sizeof(hid_descriptor_storage));
    //   hid_host_register_packet_handler(hid_report_handler);
    //
    // Then set GAP to connectable + page-scan so the gamepad can find us:
    //   gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_SNIFF_MODE);
    //   hci_set_master_slave_policy(HCI_ROLE_MASTER);
    //   gap_discoverable_control(1);
    //   gap_connectable_control(1);

    // Power on the BT controller
    hci_power_control(HCI_POWER_ON);

    printf("[bt] init complete\n");
}

// ── Run ───────────────────────────────────────────────────────────────────────
void bt_run(void) {
    // btstack_run_loop_execute() blocks forever, calling registered callbacks
    // as events arrive from the CYW43 chip.
    btstack_run_loop_execute();
}
