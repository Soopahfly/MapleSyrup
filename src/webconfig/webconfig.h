#pragma once

#include <stdbool.h>

// ── Config Mode Entry Point ───────────────────────────────────────────────────
//
// Called from main() when PIN_MODE_BTN is held low at boot.
// Serves an interactive config menu over USB CDC serial (stdin/stdout).
//
// Protocol: newline-terminated text commands, responses end with "OK" or "ERR".
// Also parseable by tools/webconfig.html via the Web Serial API in Chrome.
//
// Supported commands include global settings (deadzone, rapid-fire, etc.),
// GPIO pin assignments (getpins / setpin), and per-game remapping (game,
// setgame, delgame).  GPIO pin changes take effect after save + reboot.
//
// sd_available: true if SD card was successfully mounted (used to report
//               which VMU bank files exist on the card).
//
// Never returns — either awaits a "reboot" command (calls watchdog_reboot)
// or the user power-cycles the device.
void __attribute__((noreturn)) webconfig_run(bool sd_available);
