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
The firmware starts an `esp_console` REPL on **UART0** (GPIO43=TX, GPIO44=RX). This requires a USB-serial adapter connected to those pins since USB Serial/JTAG is disabled to allow TinyUSB to own the USB PHY.

At the `espkm>` prompt:

- `stats` shows queue/drop/overwrite counters.
- `transport` shows router state (`0=NONE`, `1=USB`, `2=BLE`, `3=BOTH`).
- `ring [N]` dumps recent debug ring entries (default 4, max 8).
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

## BLE Pairing & Bonding
BLE bonding is supported and enabled for persistent device records and encrypted links when the host requires it. The device supports Just-Works pairing (No IO) and will store bonds in NVS.

- HID reports are readable/notifiable without encryption; pairing is optional but recommended for secure connections.
- If a stale host bond causes connect/disconnect loops, the firmware can auto-delete the peer via `BLE_GAP_EVENT_REPEAT_PAIRING` handling.
- Use `ble clear` to manually clear all stored bonds.
- Pair/scan for `espkm-hid2`. If you previously paired with an `espkm` device, remove/forget it from the host to avoid duplicates.

## Stats
`ESPKM -> Stats log period` prints counters periodically:

- `matrix_drops`: matrix events dropped before entering core.
- `eventq_overflows`: dangerous event queue pressure.
- `report_overwrites`: safe latest-state-wins report coalescing.

Set `Stats log period` to `0` to disable periodic printing.
