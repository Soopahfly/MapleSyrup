# MapleSyrup

Bluetooth-to-Maple bus adapter firmware for the Raspberry Pi Pico 2 W (RP2350).

Connects Bluetooth Classic HID gamepads (PS4, PS5, 8BitDo, etc.) to a Sega Dreamcast via the Maple bus, emulating a standard controller with minimal input lag.

## Architecture

- **Core 0** — Maple bus (tight-polls PIO state machines, zero jitter)
- **Core 1** — BTstack event loop (Bluetooth HID host)
- **PIO SM 0** — Maple RX (SOF detection + bit sampling)
- **PIO SM 1** — Maple TX (pre-encoded bit streaming at 2 Mbps)

## Hardware

- Raspberry Pi Pico 2 W (RP2350 + CYW43439)
- GPIO 14 → SDCKA, GPIO 15 → SDCKB
- Maple port A connector (pin 1 = SDCKA, pin 3 = SDCKB, pin 4 = GND)

## Building

```bash
mkdir build && cd build
cmake .. -DPICO_BOARD=pico2_w -G Ninja
ninja
```

Flash `bt2maple.uf2` to the Pico 2 W via BOOTSEL mode.

## Input lag

~8–10 ms, dominated by the Bluetooth Classic HID poll interval.
The Maple response path adds <0.1 ms.
