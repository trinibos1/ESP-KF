# EFK- Esp32 Keyboard firmware

**Note:** This project originally started as ESPKM (ESP32 Keyboard something). The source code still retains the `espkm_*` prefixes, but the public name has been changed to **ESP‑KF** (ESP32 Keyboard Firmware) to better reflect its purpose.

This repository is a **work-in-progress** ESP-IDF firmware framework targeting **ESP32-S3**(more support coming soon) keyboards, inspired by QMK:

- Strict event pipeline (matrix → events → features → report → queued transports)
- Encoded keycodes (`uint16_t`)
- Dual-transport support: USB HID (TinyUSB) and BLE HID work simultaneously by default

## Quick start

1. Install ESP-IDF (v6.x recommended).
2. Configure (optional):
   - `idf.py menuconfig` for custom matrix pins or advanced settings
3. Build/flash:
   - `idf.py build flash monitor`

Both USB HID and BLE HID are **enabled by default**. 

## Docs

- `docs/GETTING_STARTED.md`
- `docs/ARCHITECTURE.md`
- `docs/DEBUGGING.md`

## 📊 HID Status

### 🔵 BLE HID
- ✅ Working
- 🔄 Stable under testing

### 🔌 USB HID
- ✅ Working
- 🔧 Under testing

### 🔀 Dual Mode
- 🔄 USB + BLE simultaneous mode implemented
- ⏳ Waiting for USB HID to become functional
- ⚙️ Currently under testing and tuning
   
## Current state
- Architecture + queues + tasks implemented
- USB HID keyboard sender implemented (using ESP-IDF TinyUSB/`esp_tinyusb`)- BLE HID keyboard sender implemented (using NimBLE)
- Dual-transport (USB + BLE) mode: both active simultaneously when available- Reference keyboard component: `keyboards/demo_macropad`
- Tap/Hold: implemented for `KC_MT()` / `KC_LT()` (P1 default; P2 permissive options wired)
- Combos: implemented for 2-key combos with the “undecided priority” rule (currently best suited for tap/hold-style combo members)
