#pragma once

// ── Maple Bus ─────────────────────────────────────────────────────────────────
// SDCKA and SDCKB must be consecutive (PIO IN/OUT base + offset).
// Matches Pico2Maple-fw standard layout: GPIO 16 (SDCKA) and GPIO 17 (SDCKB).
// CYW43 on Pico 2 W uses GPIO 23/24/25/29 — no conflict with 16/17.
#define PIN_MAPLE_SDCKA     16
#define PIN_MAPLE_SDCKB     17      // Must be PIN_MAPLE_SDCKA + 1

// ── PIO ───────────────────────────────────────────────────────────────────────
#define MAPLE_PIO           pio0
#define MAPLE_SM_RX         0
#define MAPLE_SM_TX         1

// ── Maple Addressing ──────────────────────────────────────────────────────────
// Port A, no sub-peripherals = 0x20.
// Port A, VMU in sub-slot 1  = 0x21 (controller advertises sub1 present).
// VMU sub-peripheral address = 0x01.
#define MAPLE_ADDR_CONTROLLER   0x21    // controller with VMU attached
#define MAPLE_ADDR_VMU          0x01    // VMU sub-peripheral
#define MAPLE_ADDR_HOST         0x00

// ── SD Card (SPI1) ────────────────────────────────────────────────────────────
#define PIN_SD_SCK          10
#define PIN_SD_TX           11      // MOSI
#define PIN_SD_RX           12      // MISO
#define PIN_SD_CS           13

// ── Config Mode Button ────────────────────────────────────────────────────────
// Hold LOW at boot (short to GND) to enter the web config console.
// Internal pull-up is enabled; idle state = HIGH (not pressed).
#define PIN_MODE_BTN        22

// ── VMU SD Card Pin Aliases ───────────────────────────────────────────────────
#define VMU_SD_SCK_PIN      10
#define VMU_SD_MOSI_PIN     11
#define VMU_SD_MISO_PIN     12
#define VMU_SD_CS_PIN       13
#define VMU_SD_BAUD         (12 * 1000 * 1000)  // 12 MHz

// ── OLED Display (I2C0) ───────────────────────────────────────────────────────
#define PIN_OLED_SDA        4
#define PIN_OLED_SCL        5
#define OLED_I2C            i2c0
#define OLED_I2C_ADDR       0x3C
#define OLED_WIDTH          128
#define OLED_HEIGHT         64

// ── OLED Controller Selection ─────────────────────────────────────────────────
// 0 = SSD1306 (default — most 0.96" and some 1.3" modules)
// 1 = SH1106  (most 1.3" modules — same resolution, page-addressed GRAM)
// Future: selectable at runtime via web config UI.
#ifndef OLED_SH1106
#define OLED_SH1106         0
#endif

// ── VMU Buzzer ────────────────────────────────────────────────────────────────
// GPIO 3 matches Pico2Maple-fw standard layout (PWM slice 1 channel B).
#define PIN_BUZZER          3

// ── VMU Banks ─────────────────────────────────────────────────────────────────
// Select + R1 / L1 cycles through VMU bank slots 0-9.
// Each bank is an independent 128 KB image on the SD card.
#define VMU_NUM_BANKS       10

// ── Spinlock IDs (0-15 available to user code) ────────────────────────────────
#define CONTROLLER_SPINLOCK_ID  0
