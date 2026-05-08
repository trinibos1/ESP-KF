# Getting Started (ESP32-S3 / ProS3D Option 1)

## Build
From `C:\Users\ramou\Documents\New project 3`:

- `idf.py set-target esp32s3`
- `idf.py build`

## Flash
- Find the port with `[System.IO.Ports.SerialPort]::GetPortNames()`.
- Flash with `idf.py -p COM7 flash`.
- If Windows only exposes the port in ROM mode, hold **BOOT**, tap **RESET** or plug USB in, then flash.

## Monitor + CLI
- Connect a USB-serial adapter to **UART0** (GPIO43=TX, GPIO44=RX) for serial monitor: `idf.py -p COM7 monitor`.
- The Option 1 CLI runs on **UART0** at the `espkm>` prompt.
- Useful commands: `stats`, `transport` (0=NONE, 1=USB, 2=BLE, 3=BOTH), `ring 8`, `ble status`, `ble off`, `ble on`.

## First Bring-Up Checklist
- Boot log should reach `CLI ready on stdio` with no reboot loop.
- `stats` should print queue counters.
- `ble status` should report `advertising=1` until a BLE host connects.
- BLE should appear in scanners as `espkm-hid2`.
- Both USB and BLE HID are active by default when available (dual-transport mode).
