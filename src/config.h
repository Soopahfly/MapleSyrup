#pragma once

// ── GPIO Pin Assignments ──────────────────────────────────────────────────────
// SDCKA and SDCKB must be consecutive (PIO uses base + offset).
#define PIN_MAPLE_SDCKA     14
#define PIN_MAPLE_SDCKB     15      // Must be PIN_MAPLE_SDCKA + 1

// ── PIO Configuration ─────────────────────────────────────────────────────────
#define MAPLE_PIO           pio0
#define MAPLE_SM_RX         0       // Receives frames from the Dreamcast
#define MAPLE_SM_TX         1       // Transmits responses to the Dreamcast

// ── Maple Port Identity ───────────────────────────────────────────────────────
// We present as port A (0), sub-device 0 (direct controller, no sub-peripherals).
// Maple address byte = (port << 6) | (1 << subport)  — for port A, device 0 = 0x20
#define MAPLE_OWN_ADDR      0x20

// ── Shared State ──────────────────────────────────────────────────────────────
// Hardware spinlock ID used to protect g_controller_state between cores.
// IDs 0-15 are available to user code (16-31 reserved by SDK).
#define CONTROLLER_SPINLOCK_ID  0
