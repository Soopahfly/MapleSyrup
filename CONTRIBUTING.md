# Contributing to MapleSyrup

Thanks for your interest in contributing. This project is intentionally kept simple and open — contributions at any level are welcome.

---

## What we need most

- **Testing** with controllers not yet verified (see below)
- **Improving `tools/webconfig.html`** — it's bare-bones by design; make it nicer
- **Game profiles** — per-game button remap configs for specific titles
- **Hardware enclosure designs** — STL files, laser-cut templates
- **Bug reports** — especially anything Maple protocol related

---

## Getting started

### Prerequisites

- Raspberry Pi Pico 2 W (RP2350 + CYW43439)
- [Pico SDK](https://github.com/raspberrypi/pico-sdk) v2.x
- CMake ≥ 3.13, Ninja, arm-none-eabi-gcc

### Build

```bash
git clone --recurse-submodules https://github.com/Soopahfly/MapleSyrup.git
cd MapleSyrup
mkdir build && cd build
cmake .. -DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_BOARD=pico2_w
cmake --build . --target bt2maple
```

Flash `build/bt2maple.uf2` via BOOTSEL mode.

---

## Project structure

```
src/
  main.c                  Boot, mode detection
  config.h                All pin assignments and compile-time options
  config_store.c/h        Flash-backed runtime settings
  controller.c/h          Shared controller state (spinlock-protected)
  bluetooth/
    bt.c                  Bluepad32 platform callbacks, rumble forwarding
    hid_map.c             Gamepad → Dreamcast button/axis mapping
  maple/
    maple.c               Maple bus PIO driver (Core 0 tight-poll loop)
    maple.pio             PIO state machine programs (TX/RX)
    vmu.c                 VMU sub-peripheral emulation + SD storage
  display/
    oled.c                SSD1306 / SH1106 128×64 OLED driver
  storage/
    sd_card.c             SPI SD card driver (no-OS-FatFS)
  webconfig/
    webconfig.c           USB CDC serial config console
tools/
  webconfig.html          Browser-based config UI (Web Serial API)
docs/
  wiring/                 SVG wiring diagrams
```

---

## Coding style

- C11, `snake_case` throughout
- `#define` constants in `SCREAMING_SNAKE_CASE`
- Public API in `.h` with a brief doc comment above each function
- `printf("[module] message\n")` for all diagnostic output
- Keep Core 0 (Maple loop) free of blocking calls — anything slow belongs on Core 1 or in config mode

---

## Verified controllers

| Controller | Status |
|---|---|
| Xbox Series X/S | ✅ Tested |
| PS4 DualShock 4 | ✅ Tested |
| Xbox One | Untested |
| PS5 DualSense | Untested |
| 8BitDo SN30 Pro+ | Untested |
| Nintendo Switch Pro | Untested |
| Stadia | Untested |

If you test a controller, open a PR updating this table (even just to add ✅ or ❌).

---

## Pull request guidelines

1. **One feature or fix per PR** — keeps review manageable
2. **Builds cleanly** — run `cmake --build` before opening the PR
3. **No breaking changes to the Maple protocol** — the DC timing is tight; test on real hardware if you change `maple.c` or `maple.pio`
4. **No new dependencies without discussion** — the build is already pulling in Bluepad32 + FatFS; additions need a good reason

---

## Reporting bugs

Open a GitHub issue with:
- What you expected to happen
- What actually happened
- Your controller make/model
- Whether the OLED / SD card / config mode is involved
- UART debug output if available (GP0 TX / GP1 RX, 115200 baud)

---

## Licence

By contributing you agree your code will be released under the [MIT licence](LICENSE).
