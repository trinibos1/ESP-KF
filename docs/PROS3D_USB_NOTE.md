# ProS3D USB Note

USB HID is fully functional. Both USB and BLE HID work simultaneously in dual-transport mode.

## Known Good Smoke Tests
- `stats` returns counters.
- `transport` shows current state (1=USB, 2=BLE, 3=BOTH when both are active).
- `ble status` returns `advertising=1` before pairing, `connected=1` after.
- USB HID enumerates as a keyboard; keypresses are received by the host.
- Windows sees `COM7` as `USB Serial Device` or `USB JTAG/serial debug unit` while the board is in Option 1 mode (only if CDC console is enabled).

## USB HID Status
- ✅ Working—enumerates and sends keystrokes
- 🔄 Tested on ProS3D (ESP32-S3 with internal USB PHY)

## BLE HID Status
-✅ Working—enumerates and sends keystrokes
-🔄 Tested on ProS3D 
## Notes
- USB Serial/JTAG is **disabled** in firmware so TinyUSB can own the PHY.
- CLI runs on UART0 (GPIO43/44); use a USB-serial adapter.
- See `docs/DEBUGGING.md` for full troubleshooting.