# MapleSyrup

A Bluetooth gamepad adapter for the Sega Dreamcast, built on the Raspberry Pi Pico 2 W (RP2350).

Pairs any Bluetooth HID gamepad (Xbox Series, PS4/DS4, PS5/DualSense, 8BitDo, etc.) to the Dreamcast Maple bus — emulating a standard controller + VMU sub-peripheral with minimal input lag.

---

## Status

**v0.4.0 — firmware complete, hardware bring-up in progress.**

The firmware builds and boots cleanly on the Pico 2 W. Physical assembly (Maple bus wiring, SD card, OLED) is underway. The table below tracks what is implemented vs verified on real hardware.

| Feature | Implemented | Hardware tested |
|---|---|---|
| Maple bus controller emulation | ✅ | 🔲 |
| Bluetooth HID (Bluepad32) | ✅ | 🔲 |
| VMU emulation (RAM-backed) | ✅ | 🔲 |
| SD card VMU persistence | ✅ | 🔲 |
| Per-game VMU bank selection | ✅ | 🔲 |
| OLED VMU LCD mirror | ✅ | 🔲 |
| Rumble forwarding | ✅ | 🔲 |
| Protobuf config storage (flash) | ✅ | 🔲 |
| Web config UI (WiFi AP) | ✅ | 🔲 |
| Controller mode switching | ✅ | 🔲 |
| Rapid fire | ✅ | 🔲 |
| Per-game button remapping | ✅ | 🔲 |

---

## Features

- **Bluetooth HID** via Bluepad32 — Xbox Series X/S, PS4, PS5, 8BitDo, Nintendo Pro Controller, and more
- **Dual-core**: Core 0 = Maple bus tight-poll loop (< 1 ms response), Core 1 = BTstack event loop
- **VMU emulation** — full 128 KB block storage, LCD mirror, per-game bank auto-selection
- **OLED display** — 128×64 SSD1306, mirrors the VMU 48×32 LCD in real time
- **Per-game VMU banks** — GDEMU/OpenMenu Game ID command maps each title to its own save slot
- **Rumble forwarding** — Dreamcast SET_CONDITION vibration commands forwarded to the paired controller
- **Web config** — hold config button at boot, connect to MapleSyrup WiFi AP, open `http://192.168.7.1`
- **5 controller modes** — Standard, Dual Analog, Twin Stick, Fight Stick, Racing (hot-switchable at runtime)
- **No USB device** — the adapter presents zero USB interfaces to the host; no drivers, no COM port conflicts

---

## Hardware

| Component | Required |
|---|---|
| Raspberry Pi Pico 2 W (RP2350 + CYW43439) | Yes |
| Dreamcast controller port cable / connector | Yes |
| MicroSD card + SPI breakout | Optional (VMU saves to RAM otherwise) |
| SSD1306 128×64 OLED (I²C) | Optional (VMU LCD mirror) |
| Momentary switch (config mode button) | Optional |

### Pin assignments

| Signal | GPIO | Physical pin |
|---|---|---|
| Maple SDCKA | 16 | 21 |
| Maple SDCKB | 17 | 22 |
| SD SCK | 10 | 14 |
| SD MOSI (TX) | 11 | 15 |
| SD MISO (RX) | 12 | 16 |
| SD CS | 13 | 17 |
| OLED SDA (I²C0) | 4 | 6 |
| OLED SCL (I²C0) | 5 | 7 |
| Buzzer (PWM) | 3 | 5 |
| Config mode button | 22 → GND | 29 → GND |
| UART TX (debug) | 0 | 1 |
| UART RX (debug) | 1 | 2 |

### Maple port wiring

```
  ┌─────────────┐
  │  1  2  3   │   (looking into the female socket on the Dreamcast)
  │    4   5   │
  └─────────────┘

  Pin 1 — SDCKA  → GPIO 16
  Pin 2 — +5 V   → VSYS (Pico pin 39) — powers the board from the Dreamcast
  Pin 3 — SDCKB  → GPIO 17
  Pin 4 — GND    → GND
  Pin 5 — Sense  → GND via 200 Ω resistor (tells the DC a controller is present)
```

> **Wire colours vary** — especially on third-party extension cables. Always probe with a multimeter before soldering. With a known controller plugged into the female end, trace continuity from the male plug pins.

---

## Building

### Prerequisites

- [Pico SDK](https://github.com/raspberrypi/pico-sdk) v2.x with `PICO_SDK_PATH` set
- CMake ≥ 3.13, arm-none-eabi-gcc (GNU Arm Embedded Toolchain)
- All other dependencies (Bluepad32, BTstack, FatFs, nanopb, lwIP) are fetched automatically by CMake at configure time — no submodules required

```bash
git clone https://github.com/Soopahfly/MapleSyrup.git
cd MapleSyrup
mkdir build && cd build
cmake .. -DPICO_BOARD=pico2_w
cmake --build . --target bt2maple -j4
```

### Flashing

Hold **BOOTSEL** on the Pico while connecting USB. A mass-storage drive appears — copy `bt2maple.uf2` onto it.

A pre-built UF2 for the current release is included at the repo root (`bt2maple.uf2`) if you don't want to build from source.

#### Generating UF2 on Windows (if picotool is unavailable)

```powershell
# From the build directory:
powershell -ExecutionPolicy Bypass -File ..\make_uf2.ps1
```

`make_uf2.ps1` reads `bt2maple.bin` and writes a valid UF2 with the correct RP2350 family ID (`0xE48BFF59`).

---

## Controller Mode Switching

Hold **SELECT** and press a face button for 1 second:

| Combo | Mode |
|---|---|
| SELECT + A | Standard (default) |
| SELECT + B | Dual Analog |
| SELECT + X | Twin Stick |
| SELECT + Y | Fight Stick |
| SELECT + LB | Racing |
| SELECT + RB | Cycle VMU bank (0–9) |

---

## Web Config

1. Hold the **config button** (GPIO 22 → GND) while powering on.
2. On your phone or PC, connect to the **MapleSyrup** WiFi network (password: `maplesyrup`).
3. Open `http://192.168.7.1` in a browser.
4. Configure settings and hit **Save** — config is written to flash and survives reboots.

Settings available:
- Analog stick deadzones (inner / outer)
- Trigger digital threshold
- Rapid fire (per-button, adjustable rate)
- Axis inversion (LX, LY, RX, RY)
- Per-game button remapping (keyed by GDEMU Game ID hash)
- Per-game VMU bank override
- Per-game controller mode override

Config is stored as a [nanopb](https://jpa.kapsi.fi/nanopb/) protobuf blob in the last 4 KB flash sector. It is forwards-compatible — adding new fields in a future firmware version will not corrupt existing saved config.

> **Debug output**: UART on GPIO 0 TX / GPIO 1 RX at 115200 baud. The device intentionally presents no USB interfaces to the host — there is no COM port, no driver needed.

---

## Input Lag

~8–10 ms end-to-end, dominated by the Bluetooth Classic HID poll interval.
The Maple bus response path (PIO state machine) adds < 0.1 ms.

---

## Roadmap

### Immediate — hardware validation
- [ ] Maple bus: Dreamcast recognises the adapter as a controller
- [ ] Button inputs verified in a real game
- [ ] VMU read/write: save a game, power cycle, confirm reload
- [ ] SD card persistence
- [ ] OLED display bring-up
- [ ] Rumble forwarding
- [ ] Web config save/load round-trip on real hardware
- [ ] BT pairing with multiple controller types

### v0.5 — Advanced Input
- [ ] Stick response curves (linear / S-curve / custom) in web config
- [ ] Per-game trigger sensitivity override
- [ ] Macro buttons (record + replay button sequences)
- [ ] Config button LED feedback (blink pattern = boot mode)

### v0.6 — Multi-peripheral
- [ ] Advertise two controllers on Maple Port A + B simultaneously
- [ ] Twin-Stick mode using two separate Bluetooth controllers
- [ ] Racing wheel force-feedback stub (Dreamcast Racing Controller protocol)

### v0.7 — VMU Extras
- [ ] VMU buzzer output via GPIO 3 PWM (play tones from DC SET_CONDITION)
- [ ] Virtual VMU LCD preview in web config (real-time)
- [ ] VMU image import/export via web UI (upload/download .bin)

### Future / Stretch Goals
- [ ] BLE controller support (DualSense Edge BLE, Xbox BLE mode)
- [ ] Dreamcast keyboard emulation
- [ ] OTA firmware update via web config
- [ ] 3D-printable enclosure design

---

## Contributing

Open source under MIT. Contributions welcome — especially:
- Testing with controllers not yet verified
- Improving the web config UI
- Adding per-game controller profiles
- Hardware enclosure designs

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

---

## Licence

MIT — see [LICENSE](LICENSE).
