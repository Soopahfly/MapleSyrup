#pragma once

#include <stdbool.h>

// ── Config Mode Entry Point ───────────────────────────────────────────────────
//
// Called from main() when PIN_MODE_BTN is held low at boot.
//
// The Pico W CYW43 creates a WiFi access point "MapleSyrup" (open, no password).
// An HTTP server runs at http://192.168.7.1 serving the embedded config UI.
// Any browser on any OS works — navigate to 192.168.7.1 after connecting to
// the "MapleSyrup" WiFi network.
//
// API endpoints served:
//   GET  /            HTML config UI
//   GET  /api/config  Current global config (JSON)
//   POST /api/config  Apply global config (JSON body)
//   GET  /api/games   All game configs (JSON array)
//   GET  /api/games/:hash  Single game config
//   POST /api/games   Upsert or delete a game config
//   POST /api/save    Write config to flash
//   POST /api/reboot  Save + watchdog reboot
//
// sd_available: currently unused — reserved for future SD mount status reporting.
//
// Never returns.
void __attribute__((noreturn)) webconfig_run(bool sd_available);
