#pragma once

#include <stdint.h>
#include <stdbool.h>

// ── VMU Storage Geometry ──────────────────────────────────────────────────────
// Mirrors a real Dreamcast VMU: 200 user blocks × 512 bytes = 100 KB user data.
// Total flash image is 256 × 512 = 128 KB (includes FAT, directory, etc.).
#define VMU_BLOCK_SIZE      512
#define VMU_NUM_BLOCKS      256
#define VMU_IMAGE_SIZE      (VMU_BLOCK_SIZE * VMU_NUM_BLOCKS)   // 128 KB
#define VMU_NUM_BANKS       10   // bank slots 0-9 (per-game VMU swap)

// ── VMU LCD ───────────────────────────────────────────────────────────────────
// 48 × 32 pixels, 1 bpp, row-major.  Total: 192 bytes.
#define VMU_LCD_WIDTH       48
#define VMU_LCD_HEIGHT      32
#define VMU_LCD_BYTES       ((VMU_LCD_WIDTH * VMU_LCD_HEIGHT) / 8)  // 192

// ── Public API ────────────────────────────────────────────────────────────────

// Initialise the in-RAM VMU image (0xFF = blank/erased).
void vmu_init(void);

// Called after sd_init() to attach SD card storage and load the VMU image.
// If available=true and file not found, a blank 0xFF image is used.
void vmu_sd_attach(bool available);

// Switch to a different bank slot (0-9).  Saves current bank to SD if available,
// loads the new bank from SD (or 0xFF if not present).
void vmu_set_bank(uint8_t bank);
uint8_t vmu_get_bank(void);

// Called by maple.c to handle commands addressed to the VMU (addr 0x01).
// raw_req: pointer to request payload (after header word), req_words: payload word count.
// raw_resp: output buffer (max 256 words), returns response byte count (including CRC).
uint32_t vmu_handle_command(uint8_t cmd, uint8_t dest, uint8_t src,
                             const uint32_t *raw_req, uint8_t req_words,
                             uint8_t *raw_resp);

// Returns pointer to the 192-byte LCD framebuffer (updated on SET_CONDITION LCD writes).
const uint8_t *vmu_get_lcd(void);

// Returns true if the LCD content has changed since last call (clears the flag).
bool vmu_lcd_dirty(void);

// Called by maple.c when a GAME_ID command arrives; switches VMU bank if GDEMU is active.
void vmu_on_game_id(const uint8_t *game_id, uint8_t len);
