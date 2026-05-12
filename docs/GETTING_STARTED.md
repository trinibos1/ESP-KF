# Getting Started (ESP32-S3)

## 1) Validate and Generate Config

From `C:\Users\ramou\Documents\New project 3`:

- `python tools/espkm.py` (interactive mode)
- `python tools/espkm.py config validate`
- `python tools/espkm.py config generate`

The generated build bridge fragment is written to:
- `config/generated/sdkconfig.espkm`

## 2) Select Board Profile

- List boards: `python tools/espkm.py board list`
- Show a board: `python tools/espkm.py board show xiao_esp32s3`
- Select a board: `python tools/espkm.py board use xiao_esp32s3`

Then re-run:
- `python tools/espkm.py config validate`
- `python tools/espkm.py config generate`

## 3) Build and Flash

- `python tools/espkm.py flash`

If flashing fails due to port, set it and try again:
- `python tools/espkm.py config set serial.port=COM7`

## 4) Monitor + CLI

- `python tools/espkm.py monitor`
- Option 1 firmware CLI prompt is `espkm>`

Useful firmware CLI commands:
- `stats`
- `transport` (0=NONE, 1=USB, 2=BLE, 3=BOTH)
- `ring 8`
- `ble status`
- `ble off`
- `ble on`

## 5) Language Selection

- List languages: `python tools/espkm.py lang list`
- Select language: `python tools/espkm.py lang use en_us`

## 6) XIAO ESP32-S3 Notes

- Use a valid matrix pin map for your wiring by editing `boards/xiao_esp32s3.yaml`.
- Keep `build.target: esp32s3` in `config/flash.yaml`.
- If USB monitor/flash port changes, update `serial.port`.

## First Bring-Up Checklist

- Validation reports `Validation OK`
- `config/generated/sdkconfig.espkm` reflects expected rows/cols/pins
- Flash command runs with selected board target and serial settings
- Runtime reaches `CLI ready on stdio`
