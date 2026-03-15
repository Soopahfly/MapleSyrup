#pragma once

#include <stdint.h>

// ── Maple Commands (host → device) ────────────────────────────────────────────
#define MAPLE_CMD_DEVICE_INFO       0x01
#define MAPLE_CMD_ALL_DEVICE_INFO   0x02
#define MAPLE_CMD_RESET             0x03
#define MAPLE_CMD_SHUTDOWN          0x04
#define MAPLE_CMD_GET_CONDITION     0x09    // Poll controller / VMU state
#define MAPLE_CMD_GET_MEDIA_INFO    0x0B    // VMU: query storage geometry
#define MAPLE_CMD_BLOCK_READ        0x0D    // VMU: read 512-byte block
#define MAPLE_CMD_BLOCK_WRITE       0x0E    // VMU: write 512-byte block
#define MAPLE_CMD_SET_CONDITION     0x12    // Rumble, VMU LED, etc.
#define MAPLE_CMD_GAME_ID           0x21    // GDEMU/OpenMenu per-game VMU bank

// ── Maple Responses (device → host) ──────────────────────────────────────────
#define MAPLE_RESP_DEVICE_INFO      0x05
#define MAPLE_RESP_ALL_DEVICE_INFO  0x06
#define MAPLE_RESP_ACK              0x07
#define MAPLE_RESP_DATA             0x08
#define MAPLE_RESP_FILE_ERROR       0x09    // VMU: bad block / out of range
#define MAPLE_RESP_AGAIN            0x0A    // VMU: busy, retry

// ── Maple Function Types ──────────────────────────────────────────────────────
#define MAPLE_FUNC_CONTROLLER       0x00000001
#define MAPLE_FUNC_STORAGE          0x00000002
#define MAPLE_FUNC_LCD              0x00000004
#define MAPLE_FUNC_CLOCK            0x00000008
#define MAPLE_FUNC_VIBRATION        0x00000100

// ── Packet Header ─────────────────────────────────────────────────────────────
// Byte 0: payload length in 32-bit words (not counting header word itself)
// Byte 1: command / response code
// Byte 2: destination address
// Byte 3: source address
typedef struct __attribute__((packed)) {
    uint8_t frame_words;
    uint8_t command;
    uint8_t destination;
    uint8_t source;
} maple_header_t;

// ── Public API ────────────────────────────────────────────────────────────────
void maple_init(void);
void __attribute__((noreturn)) maple_run(void);
