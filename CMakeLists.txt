cmake_minimum_required(VERSION 3.13)

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
    src/maple/vmu.c
    src/bluetooth/bt.c
    src/bluetooth/hid_map.c
    src/display/oled.c
)

pico_generate_pio_header(bt2maple ${CMAKE_CURRENT_LIST_DIR}/src/maple/maple.pio)

target_include_directories(bt2maple PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}
)

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
    pico_btstack_classic
    pico_btstack_cyw43
    pico_cyw43_arch_none
)

pico_enable_stdio_usb(bt2maple 1)
pico_enable_stdio_uart(bt2maple 0)

pico_add_extra_outputs(bt2maple)
