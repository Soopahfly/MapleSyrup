#pragma once

#include <stdint.h>
#include "controller.h"
#include <controller/uni_gamepad.h>

// Map a Bluepad32 normalised gamepad struct to a Dreamcast controller state.
// Called from bt.c's on_controller_data callback.
//
// gp    : Bluepad32 gamepad data (axes –512..+511, triggers 0..1023, buttons bitmask)
// state : output — updated with the mapped Dreamcast controller state
void hid_map_gamepad(const uni_gamepad_t *gp, dc_controller_state_t *state);
