#include "sd_card.h"
#include "config.h"
#include "maple/vmu.h"

// no-OS-FatFS-SD-SDIO-SPI-RPi-Pico headers
#include "ff.h"
#include "diskio.h"
#include "hw_config.h"
// sd_card.h from the library (different from our sd_card.h — resolved by include order)
#include "sd_driver/sd_card.h"
#include "sd_driver/SPI/my_spi.h"

#include <string.h>
#include <stdio.h>

// ── FatFS mount object ────────────────────────────────────────────────────────
static FATFS g_fs;
static bool  g_mounted = false;

// ── Hardware configuration (no-OS-FatFS required callbacks) ──────────────────
// The library calls sd_get_num() and sd_get_by_num() to enumerate SD cards.

static spi_t g_spi_hw = {
    .hw_inst   = spi1,
    .miso_gpio = VMU_SD_MISO_PIN,
    .mosi_gpio = VMU_SD_MOSI_PIN,
    .sck_gpio  = VMU_SD_SCK_PIN,
    .baud_rate = VMU_SD_BAUD,
    .spi_mode  = 0,
    .no_miso_gpio_pull_up = false,
};

static sd_spi_if_t g_spi_if = {
    .spi      = &g_spi_hw,
    .ss_gpio  = VMU_SD_CS_PIN,
    .set_drive_strength = false,
};

static sd_card_t g_sd_card = {
    .type         = SD_IF_SPI,
    .spi_if_p     = &g_spi_if,
    .use_card_detect = false,
};

// Required by no-OS-FatFS: return the number of SD cards in the system.
size_t sd_get_num(void) { return 1; }

// Required by no-OS-FatFS: return the SD card descriptor at index num.
sd_card_t *sd_get_by_num(size_t num) {
    if (num == 0) return &g_sd_card;
    return NULL;
}

// ── Public API ────────────────────────────────────────────────────────────────
bool sd_init(void) {
    // Initialise the SPI driver and SD card hardware
    if (!sd_init_driver()) {
        printf("[sd] sd_init_driver failed\n");
        g_mounted = false;
        return false;
    }
    // Mount the FAT filesystem on drive "0:"
    FRESULT fr = f_mount(&g_fs, "0:", 1);
    if (fr != FR_OK) {
        printf("[sd] f_mount failed: %d\n", (int)fr);
        g_mounted = false;
        return false;
    }
    g_mounted = true;
    printf("[sd] mounted on SPI1 (SCK=GP%u MOSI=GP%u MISO=GP%u CS=GP%u)\n",
           VMU_SD_SCK_PIN, VMU_SD_MOSI_PIN, VMU_SD_MISO_PIN, VMU_SD_CS_PIN);
    return true;
}

bool vmu_load(uint8_t *buf, const char *filename) {
    if (!g_mounted) return false;

    FIL     fil;
    FRESULT fr = f_open(&fil, filename, FA_READ);
    if (fr == FR_NO_FILE) {
        // File doesn't exist — fill with 0xFF (blank VMU) and create it
        printf("[sd] %s not found, creating blank VMU image\n", filename);
        memset(buf, 0xFF, VMU_IMAGE_SIZE);
        fr = f_open(&fil, filename, FA_CREATE_NEW | FA_WRITE);
        if (fr != FR_OK) {
            printf("[sd] failed to create %s: %d\n", filename, (int)fr);
            return false;
        }
        UINT bw;
        fr = f_write(&fil, buf, VMU_IMAGE_SIZE, &bw);
        f_close(&fil);
        if (fr != FR_OK || bw != (UINT)VMU_IMAGE_SIZE) {
            printf("[sd] failed to write blank image: fr=%d bw=%u\n", (int)fr, (unsigned)bw);
            return false;
        }
        printf("[sd] blank VMU image written to %s\n", filename);
        return true;
    }
    if (fr != FR_OK) {
        printf("[sd] f_open %s failed: %d\n", filename, (int)fr);
        return false;
    }

    UINT br;
    fr = f_read(&fil, buf, VMU_IMAGE_SIZE, &br);
    f_close(&fil);
    if (fr != FR_OK || br != (UINT)VMU_IMAGE_SIZE) {
        printf("[sd] short read on %s: %u bytes (fr=%d)\n", filename, (unsigned)br, (int)fr);
        return false;
    }
    return true;
}

bool vmu_save(const uint8_t *buf, const char *filename) {
    if (!g_mounted) return false;

    FIL     fil;
    FRESULT fr = f_open(&fil, filename, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        printf("[sd] f_open %s for write failed: %d\n", filename, (int)fr);
        return false;
    }
    UINT bw;
    fr = f_write(&fil, buf, VMU_IMAGE_SIZE, &bw);
    f_close(&fil);
    if (fr != FR_OK || bw != (UINT)VMU_IMAGE_SIZE) {
        printf("[sd] short write on %s: %u bytes (fr=%d)\n", filename, (unsigned)bw, (int)fr);
        return false;
    }
    return true;
}
