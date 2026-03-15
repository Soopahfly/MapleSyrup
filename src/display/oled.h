#pragma once

#include <stdint.h>
#include <stdbool.h>

// SSD1306 128×64 I2C driver.
// Configured via config.h (PIN_OLED_SDA, PIN_OLED_SCL, OLED_I2C, OLED_I2C_ADDR).

// Initialise I2C and the display.  Call once from Core 0 before maple_run().
void oled_init(void);

// Render the 48×32 VMU LCD bitmap (192 bytes, row-major, 1 bpp, MSB first)
// centred on the 128×64 display.
void oled_show_vmu(const uint8_t *vmu_pixels);

// Display a short status string (e.g. mode name or bank number) on the bottom
// row using a 5×7 font.  Max 21 characters visible.
void oled_show_status(const char *text);

// Clear the display.
void oled_clear(void);
