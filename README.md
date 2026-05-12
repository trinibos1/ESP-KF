# ESPKM ESP32 Keyboard Firmware

This repository is an ESP-IDF firmware framework for ESP32 keyboards, currently focused on ESP32-S3 boards.

Key architecture points:
- Strict event pipeline (matrix -> events -> features -> report -> queued transports)
- Encoded keycodes (`uint16_t`)
- Dual transport support: USB HID (TinyUSB) and BLE HID can run simultaneously

## Quick Start

1. Install ESP-IDF (v6.x recommended).
2. Validate config: `python tools/espkm.py config validate`
3. Generate config bridge and keymap: `python tools/espkm.py config generate`
4. Flash firmware: `python tools/espkm.py flash`
5. Open monitor: `python tools/espkm.py monitor`

Windows wrapper:
- `.\espkm.ps1 <command>`

## Profile-Driven Configuration (v1)

The project uses YAML profiles to separate board and language concerns from firmware internals:

- `config/flash.yaml` -> active config (board, language, target, serial, and feature toggles)
- `boards/*.yaml` -> reusable board profiles (matrix shape + pins + target)
- board profile can carry hardware defaults (flash size, PSRAM, partition preset)
- `languages/*.yaml` -> host-layout-aware language presets

Supported board profiles:
- `demo_macropad`
- `pros3d_option1`
- `xiao_esp32s3`

CLI commands:
- `python tools/espkm.py` (interactive menu)
- `python tools/espkm.py interactive` (interactive menu)
- `python tools/espkm.py board list|show|use`
- `python tools/espkm.py lang list|show|use`
- `python tools/espkm.py config validate|print|set|generate`
- `python tools/espkm.py flash`
- `python tools/espkm.py monitor`

## Docs

- `docs/GETTING_STARTED.md`
- `docs/ARCHITECTURE.md`
- `docs/DEBUGGING.md`
- `docs/BOARD_PROFILES.md`

## Current State

- Core architecture and queueing tasks implemented
- USB HID sender implemented via ESP-IDF TinyUSB
- BLE HID sender implemented via NimBLE
- Dual transport mode active when both links are available
- Reference keyboard component: `keyboards/demo_macropad`
- Tap/Hold for `KC_MT()` and `KC_LT()` implemented
- 2-key combos implemented
