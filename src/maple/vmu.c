#include "vmu.h"
#include "maple.h"
#include "storage/sd_card.h"
#include <string.h>
#include <stdio.h>

// ── VMU Device Info Payload ───────────────────────────────────────────────────
// func: storage(0x02) | LCD(0x04) | timer(0x08) = 0x0E, big-endian in wire order
// The Maple bus sends function word big-endian: 0x00 0x00 0x00 0x0E
static const uint8_t k_vmu_device_info[] = {
    // func: storage | LCD | timer (0x0E000000 big-endian on wire)
    0x0E, 0x00, 0x00, 0x00,
    // func_data[0]: storage  0x000f4100 (little-endian in payload)
    0x00, 0x41, 0x0F, 0x00,
    // func_data[1]: LCD  0x00201f40 → 48 wide, 32 tall, 1-bit
    0x40, 0x1F, 0x20, 0x00,
    // func_data[2]: timer (unused, zero)
    0x00, 0x00, 0x00, 0x00,
    // area_code=0xFF, connector_direction=0x00
    0xFF, 0x00,
    // product_name (30 bytes, space-padded)
    'V','i','s','u','a','l',' ','M','e','m','o','r','y',' ',' ',' ',
    ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
    // license (60 bytes, space-padded)
    'L','i','c','e','n','s','e',' ','b','y',' ',
    'S','E','G','A',' ','E','n','t','e','r','p','r','i','s','e','s',',',' ','L','t','d','.',
    ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
    ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
    // standby_power: 0x007c (little-endian)
    0x7C, 0x00,
    // max_power: 0x019a (little-endian)
    0x9A, 0x01,
};

// ── VMU Memory Information Payload ───────────────────────────────────────────
// Returned in response to GET_MEMORY_INFORMATION (0x0A).
// Geometry matches a standard Dreamcast VMU.
static const uint8_t k_vmu_media_info[] = {
    // func type: storage (0x00000002 little-endian)
    0x02, 0x00, 0x00, 0x00,
    // total_size (uint16_t LE): 255 blocks
    0xFF, 0x00,
    // partition (uint16_t LE): 0
    0x00, 0x00,
    // system_area (uint16_t LE): 255
    0xFF, 0x00,
    // fat_area (uint16_t LE): FAT block offset = 254
    0xFE, 0x00,
    // num_fat_blocks (uint16_t LE): 1
    0x01, 0x00,
    // file_area (uint16_t LE): directory at 253
    0xFD, 0x00,
    // num_file_blocks (uint16_t LE): 200
    0xC8, 0x00,
    // volume_icon (uint8_t): 0
    0x00,
    // reserved (uint8_t): 0
    0x00,
    // save_area (uint16_t LE): 0
    0x00, 0x00,
    // num_save_blocks (uint16_t LE): 200
    0xC8, 0x00,
    // reserved2 (uint32_t): 0
    0x00, 0x00, 0x00, 0x00,
};

// ── State ─────────────────────────────────────────────────────────────────────
static uint8_t  g_vmu_image[VMU_IMAGE_SIZE];    // 128 KB in RP2350 SRAM
static uint8_t  g_lcd_buf[VMU_LCD_BYTES];
static bool     g_lcd_dirty;
static uint8_t  g_bank;

// Track whether SD card is available for saves
static bool     g_sd_available;
static char     g_vmu_filename[32];

// ── Helpers ───────────────────────────────────────────────────────────────────
static uint8_t vmu_crc(const uint8_t *data, uint32_t len) {
    uint8_t crc = 0;
    for (uint32_t i = 0; i < len; i++) crc ^= data[i];
    return crc;
}

// Write a response into out (raw byte buffer).
// Returns total byte count written (header + payload + CRC).
static uint32_t write_resp(uint8_t *out, uint8_t resp_code,
                            uint8_t dest, uint8_t src,
                            const uint8_t *payload, uint32_t payload_bytes) {
    uint32_t payload_words = (payload_bytes + 3) / 4;
    out[0] = (uint8_t)payload_words;
    out[1] = resp_code;
    out[2] = src;   // response: src/dest swapped relative to request
    out[3] = dest;
    if (payload && payload_bytes) {
        memcpy(out + 4, payload, payload_bytes);
        // Pad to word boundary
        uint32_t padded_payload = payload_words * 4;
        for (uint32_t i = payload_bytes; i < padded_payload; i++)
            out[4 + i] = 0;
    }
    uint32_t total = 4 + payload_words * 4;
    out[total] = vmu_crc(out, total);
    return total + 1;
}

static void save_to_sd(void) {
    if (!g_sd_available) return;
    if (!vmu_save(g_vmu_image, g_vmu_filename)) {
        printf("[vmu] SD save failed: %s\n", g_vmu_filename);
    }
}

// ── Public API ────────────────────────────────────────────────────────────────
void vmu_init(void) {
    memset(g_vmu_image, 0xFF, sizeof(g_vmu_image));  // 0xFF = blank/erased flash
    memset(g_lcd_buf, 0, sizeof(g_lcd_buf));
    g_lcd_dirty = false;
    g_bank = 0;
    g_sd_available = false;
    snprintf(g_vmu_filename, sizeof(g_vmu_filename), "vmu_a.bin");
    printf("[vmu] init: %u KB RAM image, bank 0\n", VMU_IMAGE_SIZE / 1024);
}

void vmu_sd_attach(bool available) {
    g_sd_available = available;
    if (available) {
        // Try to load VMU image from SD card
        if (!vmu_load(g_vmu_image, g_vmu_filename)) {
            printf("[vmu] no image found, using blank image\n");
            // vmu_load creates a blank file if not found; image already 0xFF
        } else {
            printf("[vmu] loaded %s from SD\n", g_vmu_filename);
        }
    }
}

void vmu_set_bank(uint8_t bank) {
    if (bank >= VMU_NUM_BANKS) return;
    // Save current bank before switching
    save_to_sd();
    // Load new bank
    g_bank = bank;
    snprintf(g_vmu_filename, sizeof(g_vmu_filename), "vmu_%c.bin", 'a' + bank);
    memset(g_vmu_image, 0xFF, sizeof(g_vmu_image));
    if (g_sd_available) {
        if (!vmu_load(g_vmu_image, g_vmu_filename)) {
            printf("[vmu] bank %u: no image found, using blank\n", bank);
        } else {
            printf("[vmu] bank %u: loaded %s\n", bank, g_vmu_filename);
        }
    }
}

uint8_t vmu_get_bank(void) { return g_bank; }

const uint8_t *vmu_get_lcd(void) { return g_lcd_buf; }

bool vmu_lcd_dirty(void) {
    bool d = g_lcd_dirty;
    g_lcd_dirty = false;
    return d;
}

void vmu_on_game_id(const uint8_t *game_id, uint8_t len) {
    // FNV-1a hash of the game ID bytes → bank index 0-9.
    // Same game always maps to the same bank; different games rarely collide.
    uint32_t hash = 2166136261u;  // FNV offset basis
    for (uint8_t i = 0; i < len; i++) {
        hash ^= game_id[i];
        hash *= 16777619u;  // FNV prime
    }
    uint8_t bank = (uint8_t)(hash % VMU_NUM_BANKS);
    printf("[vmu] game ID (%u bytes) → bank %u\n", len, bank);
    if (bank != g_bank)
        vmu_set_bank(bank);
}

// ── Command Handler ───────────────────────────────────────────────────────────
uint32_t vmu_handle_command(uint8_t cmd, uint8_t dest, uint8_t src,
                             const uint32_t *raw_req, uint8_t req_words,
                             uint8_t *out) {
    switch (cmd) {

    case MAPLE_CMD_DEVICE_INFO:
    case MAPLE_CMD_ALL_DEVICE_INFO:
        return write_resp(out, MAPLE_RESP_DEVICE_INFO, dest, src,
                          k_vmu_device_info, sizeof(k_vmu_device_info));

    case MAPLE_CMD_GET_MEDIA_INFO:
        // Return memory info for storage partition
        return write_resp(out, MAPLE_RESP_DATA, dest, src,
                          k_vmu_media_info, sizeof(k_vmu_media_info));

    case MAPLE_CMD_GET_CONDITION: {
        // VMU has no readable "condition" but some games poll it.
        // Return a minimal storage-function condition (all zero).
        uint8_t payload[8] = { 0x02, 0x00, 0x00, 0x00, 0, 0, 0, 0 };
        return write_resp(out, MAPLE_RESP_DATA, dest, src, payload, sizeof(payload));
    }

    case MAPLE_CMD_BLOCK_READ: {
        // Request payload:
        //   Word 0: func type (0x00000002 = storage)
        //   Word 1: [partition(8) | phase(8) | block_hi(8) | block_lo(8)]
        //     block_number = block_hi<<8 | block_lo
        // Response: func word + 64 bytes of block data (128 bytes / 2 phases)
        if (req_words < 2) return 0;

        const uint8_t *req_bytes = (const uint8_t *)raw_req;
        // Word 1 bytes: partition, phase, block_hi, block_lo
        uint8_t phase    = req_bytes[5];
        uint8_t block_hi = req_bytes[6];
        uint8_t block_lo = req_bytes[7];
        uint16_t block   = ((uint16_t)block_hi << 8) | block_lo;

        if (block >= VMU_NUM_BLOCKS) {
            out[0] = 0; out[1] = MAPLE_RESP_FILE_ERROR;
            out[2] = src; out[3] = dest;
            out[4] = vmu_crc(out, 4);
            return 5;
        }

        // Each block is 512 bytes, split into 2 phases of 64 bytes each
        // (The DC reads 64 bytes per transfer, so phase 0 = first 64, phase 1 = second 64)
        // Actually Dreamcast VMU block reads return 512 bytes total split over phases.
        // Phase encodes which 64-byte chunk of the 512-byte block to return.
        uint32_t offset = (uint32_t)block * VMU_BLOCK_SIZE + (uint32_t)phase * 64;

        // Build response: func word + 64 bytes
        uint8_t resp[4 + 64];
        uint32_t func_word = 0x00000002;  // storage func
        memcpy(resp, &func_word, 4);
        if (offset + 64 <= VMU_IMAGE_SIZE) {
            memcpy(resp + 4, g_vmu_image + offset, 64);
        } else {
            memset(resp + 4, 0xFF, 64);
        }
        return write_resp(out, MAPLE_RESP_DATA, dest, src, resp, sizeof(resp));
    }

    case MAPLE_CMD_BLOCK_WRITE: {
        // Request payload:
        //   Word 0: func type
        //   Word 1: [partition(8) | phase(8) | block_hi(8) | block_lo(8)]
        //   Words 2+: data bytes
        if (req_words < 2) return 0;

        const uint8_t *req_bytes = (const uint8_t *)raw_req;
        uint32_t func_type;
        memcpy(&func_type, req_bytes, 4);

        if (func_type & MAPLE_FUNC_STORAGE) {
            // Storage write: 512 bytes per block
            if (req_words < 130) return 0;  // need func + addr + 128 words data
            uint8_t phase    = req_bytes[5];
            uint8_t block_hi = req_bytes[6];
            uint8_t block_lo = req_bytes[7];
            uint16_t block   = ((uint16_t)block_hi << 8) | block_lo;

            if (block >= VMU_NUM_BLOCKS) {
                out[0] = 0; out[1] = MAPLE_RESP_FILE_ERROR;
                out[2] = src; out[3] = dest;
                out[4] = vmu_crc(out, 4);
                return 5;
            }
            uint32_t offset = (uint32_t)block * VMU_BLOCK_SIZE + (uint32_t)phase * 64;
            if (offset + 64 <= VMU_IMAGE_SIZE) {
                memcpy(g_vmu_image + offset, req_bytes + 8, 64);
            }
            // Save to SD on each write (phase 7 = last phase of 512-byte block)
            if (phase == 7) {
                save_to_sd();
            }
        } else if (func_type & MAPLE_FUNC_LCD) {
            // LCD write: 192 bytes of 48x32 1-bit pixel data
            if (req_words >= 49) {  // func(4) + 192 bytes = 196 bytes = 49 words
                memcpy(g_lcd_buf, req_bytes + 4, VMU_LCD_BYTES);
                g_lcd_dirty = true;
            }
        }
        return write_resp(out, MAPLE_RESP_ACK, dest, src, NULL, 0);
    }

    case MAPLE_CMD_SET_CONDITION:
        // VMU has no inputs to set; just acknowledge
        return write_resp(out, MAPLE_RESP_ACK, dest, src, NULL, 0);

    case MAPLE_CMD_RESET:
    case MAPLE_CMD_SHUTDOWN:
        return 0;   // no response

    default:
        printf("[vmu] unknown cmd 0x%02X\n", cmd);
        // Return ACK for unknown commands to avoid hanging the bus
        return write_resp(out, MAPLE_RESP_ACK, dest, src, NULL, 0);
    }
}
