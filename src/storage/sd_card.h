#pragma once

#include <stdint.h>
#include <stdbool.h>

// ── SD Card / FatFS Interface ─────────────────────────────────────────────────
// Uses no-OS-FatFS-SD-SDIO-SPI-RPi-Pico via SPI0 on GPIOs 10-13.
// Provides simple load/save helpers for 128 KB VMU binary images.

// Initialise SPI and mount the FAT filesystem.
// Returns true on success, false if no card or mount failed.
bool sd_init(void);

// Load a 128 KB VMU image from the SD card into buf[VMU_IMAGE_SIZE].
// If the file does not exist, creates a blank (0xFF-filled) image and writes it.
// Returns true if a valid image was loaded or created.
bool vmu_load(uint8_t *buf, const char *filename);

// Save a 128 KB VMU image from buf[VMU_IMAGE_SIZE] to the SD card.
// Returns true on success.
bool vmu_save(const uint8_t *buf, const char *filename);
