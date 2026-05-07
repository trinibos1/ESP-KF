# ProS3D USB Note (ESP32-S3 PHY Routing)

If the ProS3D only shows up in Windows when you hold **BOOT**, you are probably seeing the ROM **USB Serial/JTAG** path rather than USB-OTG/TinyUSB device mode.

## Option 1 (Current Default)
- Use **USB Serial/JTAG** for flashing, logs, and the `espkm>` CLI (always available)
- **TinyUSB HID is now enabled** - keyboard reports via USB
- **BLE HID enabled** - keyboard reports via Bluetooth
- Both transports work simultaneously (dual-transport mode)
- No eFuse changes needed (ProS3D has internal USB PHY wired correctly from factory)

## Option 2 (USB HID via TinyUSB - Enabled by Default)
The ProS3D has an internal USB PHY wired correctly from the factory, so **no eFuse changes are needed**.

- Enable TinyUSB HID in `menuconfig` (already enabled in default config)
- USB HID keyboard will work immediately when plugged in
- No permanent eFuse burns required
- Both USB and BLE HID work simultaneously (dual-transport mode)

## Known Good Smoke Tests
- `stats` returns counters.
- `transport` shows current state (1=USB, 2=BLE, 3=BOTH when both are active).
- `ble status` returns `advertising=1` before pairing.
- Windows sees `COM7` as `USB Serial Device` or `USB JTAG/serial debug unit` while the board is in Option 1 mode.
