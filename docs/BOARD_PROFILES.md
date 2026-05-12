# Board Profiles and Migration

## Current Profiles

- `demo_macropad`: reference profile for local bring-up.
- `pros3d_option1`: profile tuned for ProS3D option 1 flow.
- `xiao_esp32s3`: starting profile for Seeed Studio XIAO ESP32-S3.

For XIAO builds, select with:
- `python tools/espkm.py board use xiao_esp32s3`
- `python tools/espkm.py config validate`
- `python tools/espkm.py config generate`

## ProS3D Option 1 Notes

Transport behavior:
- USB HID and BLE HID can run at the same time (dual transport).
- `transport` CLI state values: `1=USB`, `2=BLE`, `3=BOTH`.

Known good smoke checks:
- `stats` returns counters.
- `ble status` shows advertising before pairing and connected after pairing.
- USB HID enumerates and keypresses are received by host.

Serial/console note:
- USB Serial/JTAG is disabled so TinyUSB owns the USB PHY.
- CLI runs on UART0 (GPIO43/44), so use a USB-serial adapter.

## Board profile contract

Each board profile is a YAML file in `boards/`:

```yaml
name: Demo Macropad
revision: v1
target: esp32s3
matrix:
  rows: 2
  cols: 3
  pins:
    rows: [1, 2]
    cols: [3, 4, 5]
transport:
  usb: true
  ble: true
hardware:
  flash_size_mb: 16
  psram: false
  partition_table: singleapp
usb:
  vid: 0x303A
  pid: 0x4001
  manufacturer: "EFK"
  product: "My Keyboard"
  serial: "KB-001"
ble:
  device_name: "my-espkm"
  manufacturer: "EFK"
  model: "my-board-v1"
sdkconfig_overrides:
  CONFIG_BT_NIMBLE_MAX_CONNECTIONS: 3
```

Required fields:
- `name` (string)
- `target` (`esp32s3`, `esp32`, or `esp32c3`)
- `matrix.rows` (1..8)
- `matrix.cols` (1..16)
- `matrix.pins.rows` (list length must match `rows`)
- `matrix.pins.cols` (list length must match `cols`)

Validation rules:
- GPIOs must be integers
- No duplicate pins inside rows/cols
- No overlap between row and col pins
- `hardware.flash_size_mb` must be one of `2, 4, 8, 16, 32` when set
- `hardware.psram` must be boolean when set
- `hardware.partition_table` must be `singleapp`, `default_8MB`, or `huge_app` when set
- `sdkconfig_overrides` keys must start with `CONFIG_`
- `usb.vid`/`usb.pid` must be `0..65535` when set
- `usb.manufacturer`/`usb.product`/`usb.serial` must be non-empty strings when set
- `ble.device_name`/`ble.manufacturer`/`ble.model` must be non-empty strings when set

## Language profile contract

Each language preset is in `languages/`:

```yaml
name: English (US)
host_layout: us-qwerty
description: Default US ANSI host layout pairing.
aliases:
  quote: KC_BASIC(KC_QUOTE)
```

Required fields:
- `name` (string)
- `host_layout` (string)
- `aliases` (mapping of logical alias -> keycode expression string)

Language presets are host-layout aware hints only in V1. Firmware HID emission is unchanged.

## Active flash config contract

`config/flash.yaml` selects the active board/language and flashing defaults.

Important fields:
- `board`
- `language`
- `build.target`
- `serial.port`
- `serial.baud` (optional)
- `matrix_overrides` (optional)
- `features` (optional booleans)

## Build bridge behavior

`python tools/espkm.py config generate` writes `config/generated/sdkconfig.espkm` with:
- `CONFIG_ESPKM_MATRIX_ROWS`
- `CONFIG_ESPKM_MATRIX_COLS`
- `CONFIG_ESPKM_DIRECT_ROW_PINS`
- `CONFIG_ESPKM_DIRECT_COL_PINS`
- feature toggles when set
- board hardware flags (flash size/psram/partition defaults)
- board `sdkconfig_overrides` passthrough values
- TinyUSB identity fields (VID/PID/manufacturer/product/serial)
- BLE identity fields (GAP device name + DIS manufacturer/model)

`flash` and `monitor` commands run `idf.py` with:
- `SDKCONFIG_DEFAULTS=sdkconfig.defaults;config/generated/sdkconfig.espkm`

This keeps current firmware internals and `CONFIG_ESPKM_*` usage intact while enabling profile-based selection.

## Migration (before -> after)

Before:
- Users edited matrix pins through `menuconfig`/`sdkconfig` directly.

After:
1. Add or edit a board file in `boards/<name>.yaml`.
2. Select it: `python tools/espkm.py board use <name>`.
3. Validate and generate:
   - `python tools/espkm.py config validate`
   - `python tools/espkm.py config generate`
4. Flash:
   - `python tools/espkm.py flash`

Compatibility note:
- Existing manual `sdkconfig`-based workflows still work.
- New workflow is recommended for repeatable board swapping and shareable configs.

## Default keymap YAML

Default keymap lives inside each board YAML under `keymap.layers`.

Current generation target:
- `keyboards/demo_macropad/keymaps/default/generated_keymap.h`

Generation rule:
- `python tools/espkm.py config generate` regenerates both sdkconfig bridge and generated keymap header.
