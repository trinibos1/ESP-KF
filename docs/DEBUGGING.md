# Debugging

## Logs
- Core modules use `ESP_LOGI/W/E`.
- Transport state transitions are logged by the router.
- Matrix start configuration is logged at boot.

## Debug Ring Buffer
If `ESPKM -> Enable debug ring buffer` is enabled, internal events are stored in a bounded ring buffer.

- `espkm_dbg_log(...)` records structured events.
- `espkm_dbg_dump(...)` copies recent entries for CLI/CDC dumps.

## Console CLI (Option 1 Default)
The firmware starts an `esp_console` REPL on **USB Serial/JTAG**. This works on boards where USB-OTG/TinyUSB does not enumerate yet.

At the `espkm>` prompt:

- `stats` shows queue/drop/overwrite counters.
- `transport` shows router state (`0=NONE`, `1=USB`, `2=BLE`, `3=BOTH`).
- `ring [N]` dumps recent debug ring entries.
- `ble [status|on|off]` checks or gates BLE advertising.
- `ble clear` clears stored NimBLE bonds/CCCD data.

The CLI startup is non-fatal: if stdio/console setup fails, the firmware logs a warning and keeps matrix/core/BLE running.

## Dual-Transport Mode (USB + BLE)
By default, both USB and BLE HID are active simultaneously when both connections are available:

- **Transport state `3` (BOTH)**: Both USB and BLE devices are connected and receiving keyboard reports.
- **Transport state `1` (USB only)**: Only USB is connected; USB receives reports.
- **Transport state `2` (BLE only)**: Only BLE is connected; BLE receives reports.
- **Transport state `0` (NONE)**: Neither is connected.

This allows your keyboard to send keystrokes to both a USB-connected host and a Bluetooth-connected host simultaneously.

## BLE HID Bring-Up Mode
During early HID testing, BLE bonding is disabled in firmware. This avoids stale host LTK/bond records causing repeated connect/disconnect loops while the HID GATT table is still changing.

- Pair/scan for `espkm-hid2`, not the older `espkm` name.
- If the host keeps auto-connecting to the old device, remove/forget both `espkm` and `espkm-hid2`.
- Once HID typing is stable, bonding can be re-enabled with persistent NimBLE storage.

## Stats
`ESPKM -> Stats log period` prints counters periodically:

- `matrix_drops`: matrix events dropped before entering core.
- `eventq_overflows`: dangerous event queue pressure.
- `report_overwrites`: safe latest-state-wins report coalescing.

Set `Stats log period` to `0` to disable periodic printing.
