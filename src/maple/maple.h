#pragma once

#include <stdint.h>

// ── Maple Commands (host → device) ────────────────────────────────────────────
#define MAPLE_CMD_DEVICE_INFO       0x01    // Request device identity
#define MAPLE_CMD_ALL_DEVICE_INFO   0x02    // Request extended identity
#define MAPLE_CMD_RESET             0x03
#define MAPLE_CMD_SHUTDOWN          0x04
#define MAPLE_CMD_GET_CONDITION     0x09    // Poll controller state
#define MAPLE_CMD_GET_MEDIA_INFO    0x0B
#define MAPLE_CMD_BLOCK_READ        0x0D
#define MAPLE_CMD_BLOCK_WRITE       0x0E
#define MAPLE_CMD_SET_CONDITION     0x12    // Rumble, LED, etc.

// ── Maple Responses (device → host) ──────────────────────────────────────────
#define MAPLE_RESP_DEVICE_INFO      0x05
#define MAPLE_RESP_ALL_DEVICE_INFO  0x06
#define MAPLE_RESP_ACK              0x07
#define MAPLE_RESP_DATA             0x08    // Condition / media data

// ── Maple Function Types ──────────────────────────────────────────────────────
#define MAPLE_FUNC_CONTROLLER       0x00000001
#define MAPLE_FUNC_STORAGE          0x00000002
#define MAPLE_FUNC_LCD              0x00000004
#define MAPLE_FUNC_CLOCK            0x00000008
#define MAPLE_FUNC_VIBRATION        0x00000100

// ── Packet Header ─────────────────────────────────────────────────────────────
// First word of every Maple frame (little-endian on the wire):
//   Byte 0: number of additional 32-bit payload words (0 = header only)
//   Byte 1: command / response code
//   Byte 2: destination address
//   Byte 3: source address
//
// Maple address byte: bits [7:6] = port (0-3), bits [5:0] = sub-device mask.
// Port A direct device = 0x20 (port 0, sub-device 0).
typedef struct __attribute__((packed)) {
    uint8_t frame_words;    // Payload length in 32-bit words (not counting header)
    uint8_t command;
    uint8_t destination;
    uint8_t source;
} maple_header_t;

// ── Public API ────────────────────────────────────────────────────────────────

// Configure PIO state machines and GPIO pins.  Call once from Core 0.
void maple_init(void);

// Tight-poll loop — handles all Maple bus I/O.
// Runs on Core 0.  Never returns.
void maple_run(void);
