cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w)
set(PICO_PLATFORM rp2350)

# Support both -DPICO_SDK_PATH=... and PICO_SDK_PATH env var
if(NOT DEFINED PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(bt2maple C CXX ASM)
set(CMAKE_C_STANDARD 11)

# ── Version ───────────────────────────────────────────────────────────────────
set(BT2MAPLE_VERSION_MAJOR 0)
set(BT2MAPLE_VERSION_MINOR 4)
set(BT2MAPLE_VERSION_PATCH 0)
set(BT2MAPLE_VERSION "${BT2MAPLE_VERSION_MAJOR}.${BT2MAPLE_VERSION_MINOR}.${BT2MAPLE_VERSION_PATCH}")
message(STATUS "bt2maple version: ${BT2MAPLE_VERSION}")

# ── Bluepad32 via FetchContent ─────────────────────────────────────────────────
# Must be declared before pico_sdk_init() so that BTSTACK_ROOT can be set to
# the Pico SDK's bundled btstack (matching what CYW43 arch expects).
include(FetchContent)

# Point btstack at the Pico SDK's copy so it matches what pico_btstack_* uses.
set(BTSTACK_ROOT ${PICO_SDK_PATH}/lib/btstack)

FetchContent_Declare(
    bluepad32
    GIT_REPOSITORY https://github.com/ricardoquesada/bluepad32.git
    GIT_TAG        main
    GIT_SUBMODULES ""   # Don't clone external/btstack — we use Pico SDK's copy
    SOURCE_SUBDIR  src/components/bluepad32
)
FetchContent_MakeAvailable(bluepad32)

# Declare FatFS now (before pico_sdk_init) so it is available, but defer
# MakeAvailable until after pico_sdk_init() since its CMakeLists.txt calls
# pico_generate_pio_header which requires the Pico SDK to be initialised.
FetchContent_Declare(
    no_os_fatfs
    GIT_REPOSITORY https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico.git
    GIT_TAG        main
    SOURCE_SUBDIR  src
)

FetchContent_Declare(
    nanopb
    GIT_REPOSITORY https://github.com/nanopb/nanopb.git
    GIT_TAG        0.3.9.8
    GIT_SHALLOW    TRUE
)
FetchContent_Populate(nanopb)

pico_sdk_init()

# ── no-OS-FatFS SD/SPI driver (must be after pico_sdk_init) ──────────────────
FetchContent_MakeAvailable(no_os_fatfs)

# ── nanopb protobuf runtime ────────────────────────────────────────────────
# Pre-generated .pb.c/.pb.h files are committed in src/proto/.
# The runtime itself is just three C files — no cmake library needed.
set(NANOPB_SRCS
    ${nanopb_SOURCE_DIR}/pb_encode.c
    ${nanopb_SOURCE_DIR}/pb_decode.c
    ${nanopb_SOURCE_DIR}/pb_common.c
)

# Bluepad32 needs btstack_config.h and sdkconfig.h at compile time.
# These live in src/ — propagate to the bluepad32 static-library target.
target_include_directories(bluepad32 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

# Disable asserts in Bluepad32 to avoid undefined reference to `assert` on
# bare-metal ARM (newlib assert() calls abort() which requires OS support).
target_compile_definitions(bluepad32 PRIVATE NDEBUG)

add_executable(bt2maple
    src/main.c
    src/controller.c
    src/config_store.c
    src/maple/maple.c
    src/maple/vmu.c
    src/bluetooth/bt.c
    src/bluetooth/hid_map.c
    src/display/oled.c
    src/storage/sd_card.c
    src/webconfig/webconfig.c
    # src/usb/usb_host.c  # disabled while validating BT — re-enable with tinyusb libs
    src/proto/maplesyrup.pb.c
    ${NANOPB_SRCS}
)

pico_generate_pio_header(bt2maple ${CMAKE_CURRENT_LIST_DIR}/src/maple/maple.pio)

target_include_directories(bt2maple PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}
    # Bluepad32 public headers
    ${bluepad32_SOURCE_DIR}/include
    # BTstack headers (for btstack_config.h resolution)
    ${BTSTACK_ROOT}/src
    ${BTSTACK_ROOT}/3rd-party/bluedroid/encoder/include
    ${BTSTACK_ROOT}/3rd-party/bluedroid/decoder/include
    # no-OS-FatFS headers
    ${no_os_fatfs_SOURCE_DIR}/src
    ${no_os_fatfs_SOURCE_DIR}/src/ff15
    ${no_os_fatfs_SOURCE_DIR}/src/ff15/source
    ${no_os_fatfs_SOURCE_DIR}/src/sd_driver
    ${no_os_fatfs_SOURCE_DIR}/src/sd_driver/SPI
    ${no_os_fatfs_SOURCE_DIR}/src/include
    # nanopb runtime headers
    ${nanopb_SOURCE_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src/proto
)

# Bluepad32 also needs to find sdkconfig.h / btstack_config.h from src/
# Use include_directories so the static lib inherits it.
include_directories(${CMAKE_CURRENT_LIST_DIR}/src)

target_compile_options(bt2maple PRIVATE
    -O2
    -Wall
    -Wno-unused-parameter
)

# Place time-critical maple run loop in SRAM (avoids flash wait states)
target_compile_options(bt2maple PRIVATE
    -ffunction-sections
    -fdata-sections
)

target_link_libraries(bt2maple
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_gpio
    hardware_sync
    hardware_clocks
    hardware_i2c
    hardware_pwm
    hardware_spi
    hardware_flash
    hardware_watchdog
    pico_btstack_classic
    pico_btstack_cyw43
    pico_cyw43_arch_none
    bluepad32
    no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
    # tinyusb_host and tinyusb_board re-enabled when USB host is restored
    # tinyusb_board runs USB hardware init at startup which conflicts with CYW43
    # tinyusb_host
    # tinyusb_board
)

# Enable both UART and USB CDC stdio.
# UART (GP0 TX / GP1 RX) carries debug output.
# USB CDC also active — used as the config-mode console when PIN_MODE_BTN is held.
pico_enable_stdio_usb(bt2maple 1)
pico_enable_stdio_uart(bt2maple 1)

pico_add_extra_outputs(bt2maple)

# ── Embed version info into binary (readable via picotool info) ───────────────
pico_set_program_name(bt2maple "bt2maple")
pico_set_program_version(bt2maple "${BT2MAPLE_VERSION}")
pico_set_program_description(bt2maple "Bluetooth/USB to Dreamcast Maple bus adapter")
pico_set_program_url(bt2maple "https://github.com/Soopahfly/MapleSyrup")

# Pass version to C code for runtime reporting
target_compile_definitions(bt2maple PRIVATE
    BT2MAPLE_VERSION_MAJOR=${BT2MAPLE_VERSION_MAJOR}
    BT2MAPLE_VERSION_MINOR=${BT2MAPLE_VERSION_MINOR}
    BT2MAPLE_VERSION_PATCH=${BT2MAPLE_VERSION_PATCH}
    BT2MAPLE_VERSION_STRING="${BT2MAPLE_VERSION}"
    CONFIG_VERSION=2
)
