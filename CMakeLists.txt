cmake_minimum_required(VERSION 3.13)

# Must be set before pico_sdk_import and project()
set(PICO_BOARD pico2_w)
set(PICO_PLATFORM rp2350)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(bt2maple C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(bt2maple
    src/main.c
    src/controller.c
    src/maple/maple.c
    src/bluetooth/bt.c
    src/bluetooth/hid_map.c
)

# Generate PIO header from maple.pio
pico_generate_pio_header(bt2maple ${CMAKE_CURRENT_LIST_DIR}/src/maple/maple.pio)

target_include_directories(bt2maple PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src   # for config.h, controller.h, etc.
    ${CMAKE_CURRENT_LIST_DIR}       # for btstack_config.h (required by BTstack)
)

target_compile_options(bt2maple PRIVATE
    -O2
    -Wall
)

target_link_libraries(bt2maple
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_gpio
    hardware_sync
    hardware_clocks
    pico_btstack_classic
    pico_btstack_cyw43
    pico_cyw43_arch_none        # No lwIP; we drive BTstack manually on Core 1
)

# Route stdio over USB (useful for debugging without UART wires)
pico_enable_stdio_usb(bt2maple 1)
pico_enable_stdio_uart(bt2maple 0)

# Produce .uf2 / .bin / .hex / .map
pico_add_extra_outputs(bt2maple)
