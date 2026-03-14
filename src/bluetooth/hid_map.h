#pragma once

#include <stdint.h>
#include "controller.h"

// Parse a raw HID report from a Bluetooth gamepad and write the result into
// *state.  The caller is responsible for calling controller_state_update()
// afterwards.
//
// report : pointer to the raw HID report bytes
// len    : report length in bytes
// state  : output — updated with the mapped Dreamcast controller state
void hid_map_report(const uint8_t *report, uint16_t len,
                    dc_controller_state_t *state);
