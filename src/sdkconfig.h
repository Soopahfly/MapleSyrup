#pragma once
// Bluepad32 "menuconfig" for Pico 2 W (RP2350)

#define CONFIG_BLUEPAD32_MAX_DEVICES            4
#define CONFIG_BLUEPAD32_MAX_ALLOWLIST          4
#define CONFIG_BLUEPAD32_GAP_SECURITY           1
#define CONFIG_BLUEPAD32_ENABLE_BLE_BY_DEFAULT  1

// Use the custom platform callback mechanism
#define CONFIG_BLUEPAD32_PLATFORM_CUSTOM

// Target board
#define CONFIG_TARGET_PICO_W

// Log level: 2 = Info
#define CONFIG_BLUEPAD32_LOG_LEVEL 2
