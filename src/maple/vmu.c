#include "vmu.h"
#include "maple.h"
#include <string.h>
#include <stdio.h>

// ── VMU Device Info Payload ───────────────────────────────────────────────────
// Advertises: Storage + LCD + Clock functions (matches a real VMU).
static const uint8_t k_vmu_device_info[] = {
    // func: storage | LCD | clock
    0x0E, 0x00, 0x00, 0x00,
    // func_data[0]: storage geometry
    //   [31:24] = partition count - 1 (0 = 1 partition)
    //   [23:16] = system area start block
    //   [15:8]  = FAT offset
    //   [7:0]   = number of FAT blocks
    0x00, 0x1F, 0x00, 0xFF,
    // func_data[1]: LCD geometry
    //   [31:24] = width  (48)
    //   [23:16] = height (32)
    //   [15:8]  = colour depth (1 = 1bpp)
    //   [7:0]   = 0x00
    0x30, 0x20, 0x01, 0x00,
    // func_data[2]: clock (unused, zero)
    0x00, 0x00, 0x00, 0x00,
    // area_code, connector_direction
    0xFF, 0x00,
    // product_name (30 bytes)
    'V','i','s','u','a','l',' ','M','e','m','o','r','y',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    // license (60 bytes)
    'L','i','c','e','n','s','e','d',' ','b','y',' ',
    'S','E','G','A',' ','E','n','t','e','r','p','r','i','s','e','s',',',' ','L','t','d',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    // standby_mw, max_mw
    0x01, 0x00,
    0x02, 0x00,
};

// ── VMU Media Info Payload ────────────────────────────────────────────────────
// Returned in response to GET_MEDIA_INFO.  Geometry matches a standard VMU.
static const uint8_t k_vmu_media_info[] = {
    // func type
    0x02, 0x00, 0x00, 0x00,
    // total_size (uint16_t LE): 256 blocks
    0x00, 0x01,
    // partition_size (uint16_t LE): 256 blocks
    0x00, 0x01,
    // root_block (uint16_t LE): block 255 (last block is root)
    0xFF, 0x00,
    // fat_offset (uint16_t LE): block 254
    0xFE, 0x00,
    // fat_size (uint16_t LE): 1 FAT block
    0x01, 0x00,
    // file_info_offset (uint16_t LE): block 253
    0xFD, 0x00,
    // file_info_size (uint16_t LE): 13 blocks
    0x0D, 0x00,
    // volume_icon (uint8_t): 0
    0x00,
    // reserved
    0x00,
    // save_count (uint16_t LE): 200 user blocks
    0xC8, 0x00,
    // extra_offset (uint16_t LE): 0
    0x00, 0x00,
    // extra_size (uint16_t LE): 0
    0x00, 0x00,
};

// ── State ─────────────────────────────────────────────────────────────────────
static uint8_t  g_vmu_image[VMU_IMAGE_SIZE];    // 128 KB in RP2350 SRAM
static uint8_t  g_lcd_buf[VMU_LCD_BYTES];
static bool     g_lcd_dirty;
static uint8_t  g_bank;

// ── Helpers ───────────────────────────────────────────────────────────────────
static uint8_t vmu_crc(const uint8_t *data, uint32_t len) {
    uint8_t crc = 0;
    for (uint32_t i = 0; i < len; i++) crc ^= data[i];
    return crc;
}

// Write a response into raw_resp (raw byte buffer).
// Returns total byte count written (header + payload + CRC).
static uint32_t write_resp(uint8_t *out, uint8_t resp_code,
                            uint8_t dest, uint8_t src,
                            const uint8_t *payload, uint32_t payload_bytes) {
    uint32_t payload_words = (payload_bytes + 3) / 4;
    out[0] = (uint8_t)payload_words;
    out[1] = resp_code;
    out[2] = src;   // src/dest swapped
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

// ── Public API ────────────────────────────────────────────────────────────────
void vmu_init(void) {
    memset(g_vmu_image, 0, sizeof(g_vmu_image));
    memset(g_lcd_buf, 0, sizeof(g_lcd_buf));
    g_lcd_dirty = false;
    g_bank = 0;
    printf("[vmu] init: %u KB RAM image, bank 0\n", VMU_IMAGE_SIZE / 1024);
}

void vmu_set_bank(uint8_t bank) {
    if (bank >= VMU_NUM_BANKS) return;
    // TODO: flush current bank to SD card, load new bank from SD
    memset(g_vmu_image, 0, sizeof(g_vmu_image));
    g_bank = bank;
    printf("[vmu] switched to bank %u\n", bank);
}

uint8_t vmu_get_bank(void) { return g_bank; }

const uint8_t *vmu_get_lcd(void) { return g_lcd_buf; }

bool vmu_lcd_dirty(void) {
    bool d = g_lcd_dirty;
    g_lcd_dirty = false;
    return d;
}

void vmu_on_game_id(const uint8_t *game_id, uint8_t len) {
    // TODO: hash game_id to select a bank (0-9) for per-game VMU
    (void)game_id; (void)len;
    printf("[vmu] game ID received (%u bytes) — per-game bank not yet implemented\n", len);
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
        return write_resp(out, MAPLE_RESP_DATA, dest, src,
                          k_vmu_media_info, sizeof(k_vmu_media_info));

    case MAPLE_CMD_GET_CONDITION: {
        // VMU has no readable "condition" but some games poll it.
        // Return a minimal storage-function condition (all zero).
        uint8_t payload[8] = { 0x02,0x00,0x00,0x00, 0,0,0,0 };
        return write_resp(out, MAPLE_RESP_DATA, dest, src, payload, sizeof(payload));
    }

    case MAPLE_CMD_BLOCK_READ: {
        // Request payload (after function word):
        //   Word 0: func type (0x00000002)
        //   Word 1: [partition<<24 | phase<<16 | block_hi<<8 | block_lo]
        // Response: func word + 128 words (512 bytes) of block data
        if (req_words < 2) return 0;
        uint32_t req1 = raw_req[1];
        uint16_t block = (uint16_t)((req1 >> 8) & 0xFFFF);
        if (block >= VMU_NUM_BLOCKS) {
            out[0] = 0; out[1] = MAPLE_RESP_FILE_ERROR; out[2] = src; out[3] = dest;
            out[4] = vmu_crc(out, 4);
            return 5;
        }
        // Build response: func word + 512 bytes of block data
        uint8_t resp[4 + 4 + VMU_BLOCK_SIZE];
        uint32_t func_word = 0x00000002;
        memcpy(resp, &func_word, 4);
        memcpy(resp + 4, g_vmu_image + block * VMU_BLOCK_SIZE, VMU_BLOCK_SIZE);
        return write_resp(out, MAPLE_RESP_DATA, dest, src, resp, sizeof(resp));
    }

    case MAPLE_CMD_BLOCK_WRITE: {
        // Request payload:
        //   Word 0: func type
        //   Word 1: [partition<<24 | phase<<16 | block_hi<<8 | block_lo]
        //   Words 2..129: 512 bytes of data
        if (req_words < 130) return 0;
        uint32_t req1 = raw_req[1];
        uint16_t block = (uint16_t)((req1 >> 8) & 0xFFFF);
        if (block >= VMU_NUM_BLOCKS) {
            out[0] = 0; out[1] = MAPLE_RESP_FILE_ERROR; out[2] = src; out[3] = dest;
            out[4] = vmu_crc(out, 4);
            return 5;
        }
        memcpy(g_vmu_image + block * VMU_BLOCK_SIZE, &raw_req[2], VMU_BLOCK_SIZE);
        // ACK (no payload)
        return write_resp(out, MAPLE_RESP_ACK, dest, src, NULL, 0);
    }

    case MAPLE_CMD_SET_CONDITION: {
        // LCD write: func word 0x00000004, then 192 bytes of pixel data
        if (req_words < 1) return 0;
        uint32_t func = raw_req[0];
        if ((func & MAPLE_FUNC_LCD) && req_words >= 49 /* 4+192 bytes = 49 words */) {
            memcpy(g_lcd_buf, ((const uint8_t *)raw_req) + 4, VMU_LCD_BYTES);
            g_lcd_dirty = true;
        }
        return write_resp(out, MAPLE_RESP_ACK, dest, src, NULL, 0);
    }

    case MAPLE_CMD_RESET:
    case MAPLE_CMD_SHUTDOWN:
        return 0;   // no response

    default:
        printf("[vmu] unknown cmd 0x%02X\n", cmd);
        return 0;
    }
}
