# MapleSyrup

A Bluetooth gamepad adapter for the Sega Dreamcast, built on the Raspberry Pi Pico 2 W (RP2350).

Pairs any Bluetooth HID gamepad (Xbox Series, PS4/DS4, PS5/DualSense, 8BitDo, etc.) to the Dreamcast Maple bus — emulating a standard controller + VMU sub-peripheral with minimal input lag.

---

## Features

- **Bluetooth HID** via Bluepad32 — Xbox Series X/S, PS4, PS5, 8BitDo, Nintendo Pro Controller, and more
- **Dual-core**: Core 0 = Maple bus tight-poll loop (< 1 ms response), Core 1 = BTstack event loop
- **VMU emulation** — full 128 KB block storage, LCD mirror, per-game bank auto-selection
- **OLED display** — 128×64 SSD1306, mirrors the VMU 48×32 LCD in real time
- **Per-game VMU banks** — GDEMU/OpenMenu Game ID command maps each title to its own save slot
- **Rumble forwarding** — Dreamcast SET_CONDITION vibration commands forwarded to the paired controller
- **Web config console** — hold config button at boot, connect USB, open `tools/webconfig.html` in Chrome
- **5 controller modes** — Standard, Dual Analog, Twin Stick, Fight Stick, Racing (hot-switchable at runtime)

---

## Hardware

| Component | Required |
|---|---|
| Raspberry Pi Pico 2 W (RP2350 + CYW43439) | Yes |
| Maple bus connector (female MX 5-pin) | Yes |
| MicroSD card + SPI breakout | Optional (VMU saves) |
| SSD1306 128×64 OLED (I2C) | Optional (VMU LCD mirror) |
| Momentary switch (config mode) | Optional |

### Pin assignments

| Signal | GPIO |
|---|---|
| Maple SDCKA | 6 |
| Maple SDCKB | 7 |
| SD SCK | 10 |
| SD MOSI | 11 |
| SD MISO | 12 |
| SD CS | 13 |
| OLED SDA (I2C0) | 4 |
| OLED SCL (I2C0) | 5 |
| Config mode button | 22 → GND |
| UART TX (debug) | 0 |
| UART RX (debug) | 1 |

Maple port wiring: pin 1 = SDCKA, pin 3 = SDCKB, pin 4 = GND, pin 5 = +5 V (do not connect +5 V to Pico — use bus power only for reference).

---

## Building

```bash
git clone --recurse-submodules https://github.com/Soopahfly/MapleSyrup.git
cd MapleSyrup
mkdir build && cd build
cmake .. -DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_BOARD=pico2_w
cmake --build . --target bt2maple
```

Flash `bt2maple.uf2` to the Pico 2 W by holding BOOTSEL while connecting USB, then copying the UF2 to the mass-storage drive that appears.

---

## Controller Mode Switching

Hold **SELECT** and press a face button for 1 second to switch mode:

| Combo | Mode |
|---|---|
| SELECT + A | Standard (default) |
| SELECT + B | Dual Analog |
| SELECT + X | Twin Stick |
| SELECT + Y | Fight Stick |
| SELECT + LB | Racing |
| SELECT + RB | Cycle VMU bank (0–9) |

---

## Web Config Console

1. Hold the **config button** (GPIO 22 shorted to GND) while plugging the Pico into USB.
2. Open `tools/webconfig.html` from this repository in **Chrome or Edge** (Web Serial API required).
3. Click **Connect via USB Serial** and select the MapleSyrup device.
4. Configure settings, click **Save & Reboot** when done.

Settings available:
- Analog stick deadzones (inner / outer)
- Trigger digital threshold
- Rapid fire (per-button, adjustable rate)
- Axis inversion (LX, LY, RX, RY)
- Per-game button remapping (keyed by GDEMU Game ID hash)
- Per-game VMU bank override
- Per-game controller mode override

Config is stored in the last 4 KB sector of the Pico's internal flash — survives power cycles and firmware updates that don't erase that sector.

---

## Input Lag

~8–10 ms end-to-end, dominated by the Bluetooth Classic HID poll interval.
The Maple response path (PIO state machine) adds < 0.1 ms.

---

## Roadmap

Planned features roughly in priority order. Pull requests welcome.

### v0.3 — Config & UX Polish
- [ ] Web Serial API tool improvements (drag-and-drop button remap, live preview)
- [ ] OLED status screen: show current mode, BT connection state, VMU bank
- [ ] Persist controller mode selection across reboots (store in config flash)
- [ ] Config button hold-duration LED feedback (blink count = boot mode)

### v0.4 — USB Wired Controller Support
- [ ] Re-enable TinyUSB host mode (resolve CYW43 init ordering conflict)
- [ ] Support wired USB HID controllers via the Pico's USB port
- [ ] Auto-detect USB vs Bluetooth and route accordingly

### v0.5 — Advanced Input
- [ ] Stick response curves (linear / S-curve / custom) in config tool
- [ ] Per-game trigger sensitivity override
- [ ] Turbo / auto-fire scheduler with adjustable duty cycle
- [ ] Macro buttons (record + replay button sequences)

### v0.6 — Multi-peripheral
- [ ] Advertise two controllers on Maple Port A + B simultaneously
- [ ] Twin-Stick mode using two separate Bluetooth controllers
- [ ] Racing wheel mode with force-feedback stub (Dreamcast Racing Controller protocol)

### v0.7 — VMU Extras
- [ ] VMU buzzer output via GPIO 26 PWM (play tones from SET_CONDITION)
- [ ] Virtual VMU LCD preview in the web config tool (real-time via Web Serial)
- [ ] VMU image import/export over USB (drag .bin onto config page)
- [ ] Multiple VMU slots selectable per-game via config (not just per-hash)

### Future / Stretch Goals
- [ ] BLE controller support (DualSense Edge BLE, Xbox BLE mode)
- [ ] Dreamcast keyboard emulation (for typing games / DC keyboard peripheral)
- [ ] Arcade stick mode with 6-button layout auto-detection
- [ ] OTA firmware update via the web config page

---

## Contributing

This project is open source (MIT). Contributions to any layer are welcome — especially:
- Improving the `tools/webconfig.html` UI (it's intentionally bare-bones)
- Adding controller profiles for specific games
- Hardware enclosure designs
- Testing with controllers not yet verified

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines (coming soon).

---

## Licence

MIT — see [LICENSE](LICENSE).
