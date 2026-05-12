#!/usr/bin/env python3
"""ESPKM profile/config CLI.

V1 scope:
- Board and language profile selection
- Strict config/profile validation
- Build bridge generation via sdkconfig defaults fragment
- Flash/monitor wrappers around idf.py
"""

from __future__ import annotations

import argparse
import copy
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Tuple

import yaml

SUPPORTED_TARGETS = {"esp32s3", "esp32", "esp32c3"}
SUPPORTED_FLASH_MB = {2, 4, 8, 16, 32}
ROOT = Path(__file__).resolve().parent.parent
CONFIG_DIR = ROOT / "config"
FLASH_CONFIG_PATH = CONFIG_DIR / "flash.yaml"
BOARDS_DIR = ROOT / "boards"
LANGUAGES_DIR = ROOT / "languages"
GENERATED_DIR = CONFIG_DIR / "generated"
GENERATED_SDKCONFIG = GENERATED_DIR / "sdkconfig.espkm"
GENERATED_KEYMAP = ROOT / "keyboards" / "demo_macropad" / "keymaps" / "default" / "generated_keymap.h"


class ValidationError(Exception):
    pass


@dataclass
class LoadedProfiles:
    flash: Dict[str, Any]
    board: Dict[str, Any]
    language: Dict[str, Any]
    board_name: str
    language_name: str


def _load_yaml(path: Path) -> Dict[str, Any]:
    if not path.exists():
        raise ValidationError(f"Missing file: {path}")
    with path.open("r", encoding="utf-8") as f:
        data = yaml.safe_load(f)
    if not isinstance(data, dict):
        raise ValidationError(f"{path} must contain a YAML mapping at top level")
    return data


def _require(d: Dict[str, Any], key: str, where: str) -> Any:
    if key not in d:
        raise ValidationError(f"{where}: missing required field '{key}'")
    return d[key]


def _require_type(value: Any, t: type, label: str) -> None:
    if not isinstance(value, t):
        raise ValidationError(f"{label}: expected {t.__name__}, got {type(value).__name__}")


def _validate_pin_list(pins: Any, field: str) -> List[int]:
    if not isinstance(pins, list) or not pins:
        raise ValidationError(f"{field}: expected non-empty list of GPIO numbers")
    out: List[int] = []
    for idx, p in enumerate(pins):
        if not isinstance(p, int):
            raise ValidationError(f"{field}[{idx}]: expected int GPIO")
        if p < 0 or p > 48:
            raise ValidationError(f"{field}[{idx}]: GPIO {p} out of range 0..48")
        out.append(p)
    if len(set(out)) != len(out):
        raise ValidationError(f"{field}: duplicate GPIOs are not allowed")
    return out


def _validate_board(board_name: str, board: Dict[str, Any]) -> None:
    _require(board, "name", f"board '{board_name}'")
    target = _require(board, "target", f"board '{board_name}'")
    if not isinstance(target, str) or target not in SUPPORTED_TARGETS:
        raise ValidationError(
            f"board '{board_name}': target must be one of {sorted(SUPPORTED_TARGETS)}"
        )
    matrix = _require(board, "matrix", f"board '{board_name}'")
    _require_type(matrix, dict, f"board '{board_name}'.matrix")
    rows = _require(matrix, "rows", f"board '{board_name}'.matrix")
    cols = _require(matrix, "cols", f"board '{board_name}'.matrix")
    if not isinstance(rows, int) or rows < 1 or rows > 8:
        raise ValidationError(f"board '{board_name}': matrix.rows must be int in 1..8")
    if not isinstance(cols, int) or cols < 1 or cols > 16:
        raise ValidationError(f"board '{board_name}': matrix.cols must be int in 1..16")
    pins = _require(matrix, "pins", f"board '{board_name}'.matrix")
    _require_type(pins, dict, f"board '{board_name}'.matrix.pins")
    row_pins = _validate_pin_list(_require(pins, "rows", f"board '{board_name}'.matrix.pins"), "matrix.pins.rows")
    col_pins = _validate_pin_list(_require(pins, "cols", f"board '{board_name}'.matrix.pins"), "matrix.pins.cols")
    if len(row_pins) != rows:
        raise ValidationError(
            f"board '{board_name}': matrix.rows={rows} but rows pin count is {len(row_pins)}"
        )
    if len(col_pins) != cols:
        raise ValidationError(
            f"board '{board_name}': matrix.cols={cols} but cols pin count is {len(col_pins)}"
        )
    all_pins = row_pins + col_pins
    if len(set(all_pins)) != len(all_pins):
        raise ValidationError(f"board '{board_name}': row/col pins overlap; pins must be unique")

    hardware = board.get("hardware", {})
    if hardware:
        _require_type(hardware, dict, f"board '{board_name}'.hardware")
        flash_mb = hardware.get("flash_size_mb")
        if flash_mb is not None:
            if not isinstance(flash_mb, int) or flash_mb not in SUPPORTED_FLASH_MB:
                raise ValidationError(
                    f"board '{board_name}': hardware.flash_size_mb must be one of {sorted(SUPPORTED_FLASH_MB)}"
                )
        if "psram" in hardware and not isinstance(hardware["psram"], bool):
            raise ValidationError(f"board '{board_name}': hardware.psram must be bool")
        partition = hardware.get("partition_table")
        if partition is not None and (
            not isinstance(partition, str) or partition not in {"singleapp", "default_8MB", "huge_app"}
        ):
            raise ValidationError(
                f"board '{board_name}': hardware.partition_table must be one of "
                "singleapp|default_8MB|huge_app"
            )

    sdk_overrides = board.get("sdkconfig_overrides", {})
    if sdk_overrides:
        _require_type(sdk_overrides, dict, f"board '{board_name}'.sdkconfig_overrides")
        for k in sdk_overrides:
            if not isinstance(k, str) or not k.startswith("CONFIG_"):
                raise ValidationError(
                    f"board '{board_name}': sdkconfig_overrides keys must be CONFIG_* strings"
                )

    usb = board.get("usb", {})
    if usb:
        _require_type(usb, dict, f"board '{board_name}'.usb")
        if "vid" in usb and (not isinstance(usb["vid"], int) or usb["vid"] < 0 or usb["vid"] > 0xFFFF):
            raise ValidationError(f"board '{board_name}': usb.vid must be int 0..65535")
        if "pid" in usb and (not isinstance(usb["pid"], int) or usb["pid"] < 0 or usb["pid"] > 0xFFFF):
            raise ValidationError(f"board '{board_name}': usb.pid must be int 0..65535")
        for k in ("manufacturer", "product", "serial"):
            if k in usb and (not isinstance(usb[k], str) or not usb[k]):
                raise ValidationError(f"board '{board_name}': usb.{k} must be non-empty string")

    ble = board.get("ble", {})
    if ble:
        _require_type(ble, dict, f"board '{board_name}'.ble")
        for k in ("device_name", "manufacturer", "model"):
            if k in ble and (not isinstance(ble[k], str) or not ble[k]):
                raise ValidationError(f"board '{board_name}': ble.{k} must be non-empty string")

    keymap = _require(board, "keymap", f"board '{board_name}'")
    _require_type(keymap, dict, f"board '{board_name}'.keymap")
    layers = _require(keymap, "layers", f"board '{board_name}'.keymap")
    if not isinstance(layers, list) or not layers:
        raise ValidationError(f"board '{board_name}': keymap.layers must be a non-empty list")
    for li, layer in enumerate(layers):
        if not isinstance(layer, dict):
            raise ValidationError(f"board '{board_name}': keymap.layers[{li}] must be mapping")
        layer_rows = _require(layer, "rows", f"board '{board_name}': keymap.layers[{li}]")
        if not isinstance(layer_rows, list) or len(layer_rows) != rows:
            raise ValidationError(
                f"board '{board_name}': keymap.layers[{li}].rows must have {rows} rows"
            )
        for ri, row_vals in enumerate(layer_rows):
            if not isinstance(row_vals, list) or len(row_vals) != cols:
                raise ValidationError(
                    f"board '{board_name}': keymap.layers[{li}].rows[{ri}] must have {cols} entries"
                )
            for ci, tok in enumerate(row_vals):
                if not isinstance(tok, str) or not tok:
                    raise ValidationError(
                        f"board '{board_name}': keymap.layers[{li}].rows[{ri}][{ci}] must be non-empty string"
                    )


def _validate_language(lang_name: str, lang: Dict[str, Any]) -> None:
    _require(lang, "name", f"language '{lang_name}'")
    host_layout = _require(lang, "host_layout", f"language '{lang_name}'")
    if not isinstance(host_layout, str) or not host_layout:
        raise ValidationError(f"language '{lang_name}': host_layout must be non-empty string")
    aliases = _require(lang, "aliases", f"language '{lang_name}'")
    _require_type(aliases, dict, f"language '{lang_name}'.aliases")
    for key, val in aliases.items():
        if not isinstance(key, str) or not key:
            raise ValidationError(f"language '{lang_name}': alias keys must be non-empty strings")
        if not isinstance(val, str) or not val:
            raise ValidationError(f"language '{lang_name}': alias '{key}' value must be non-empty string")


def _load_profiles(flash_path: Path | None = None) -> LoadedProfiles:
    if flash_path is None:
        flash_path = FLASH_CONFIG_PATH
    flash = _load_yaml(flash_path)
    board_name = _require(flash, "board", str(flash_path))
    lang_name = _require(flash, "language", str(flash_path))
    if not isinstance(board_name, str) or not board_name:
        raise ValidationError("config/flash.yaml: 'board' must be non-empty string")
    if not isinstance(lang_name, str) or not lang_name:
        raise ValidationError("config/flash.yaml: 'language' must be non-empty string")

    board = _load_yaml(BOARDS_DIR / f"{board_name}.yaml")
    lang = _load_yaml(LANGUAGES_DIR / f"{lang_name}.yaml")
    return LoadedProfiles(
        flash=flash,
        board=board,
        language=lang,
        board_name=board_name,
        language_name=lang_name,
    )


def _validate_flash(flash: Dict[str, Any], board: Dict[str, Any], lang: Dict[str, Any]) -> List[str]:
    warnings: List[str] = []
    _require(flash, "project", "config/flash.yaml")
    if not isinstance(flash["project"], str) or not flash["project"]:
        raise ValidationError("config/flash.yaml: 'project' must be non-empty string")

    build = _require(flash, "build", "config/flash.yaml")
    _require_type(build, dict, "config/flash.yaml.build")
    target = _require(build, "target", "config/flash.yaml.build")
    if target != board.get("target"):
        raise ValidationError(
            f"config/flash.yaml: build.target='{target}' does not match selected board target '{board.get('target')}'"
        )
    if target not in SUPPORTED_TARGETS:
        raise ValidationError(
            f"config/flash.yaml: build.target must be one of {sorted(SUPPORTED_TARGETS)}"
        )

    matrix_overrides = flash.get("matrix_overrides", {})
    if matrix_overrides:
        _require_type(matrix_overrides, dict, "config/flash.yaml.matrix_overrides")
        for key in ("rows", "cols"):
            if key in matrix_overrides and (
                not isinstance(matrix_overrides[key], int) or matrix_overrides[key] < 1
            ):
                raise ValidationError(
                    f"config/flash.yaml.matrix_overrides.{key}: expected positive int"
                )

    features = flash.get("features", {})
    if features:
        _require_type(features, dict, "config/flash.yaml.features")
        for k, v in features.items():
            if not isinstance(v, bool):
                raise ValidationError(f"config/flash.yaml.features.{k}: expected bool")

    transport = board.get("transport", {})
    if transport and not isinstance(transport, dict):
        raise ValidationError("board transport must be a mapping")

    keymap_hints = flash.get("keymap_hints", {})
    if keymap_hints and isinstance(keymap_hints, dict):
        expected_layout = keymap_hints.get("host_layout")
        if isinstance(expected_layout, str) and expected_layout != lang.get("host_layout"):
            warnings.append(
                "Selected language host_layout does not match keymap_hints.host_layout; "
                "verify host OS keyboard layout pairing."
            )

    return warnings


def validate_all(flash_path: Path | None = None) -> Tuple[LoadedProfiles, List[str]]:
    loaded = _load_profiles(flash_path=flash_path)
    _validate_board(loaded.board_name, loaded.board)
    _validate_language(loaded.language_name, loaded.language)
    warnings = _validate_flash(loaded.flash, loaded.board, loaded.language)
    return loaded, warnings


def _join_pins(pins: List[int]) -> str:
    return ",".join(str(x) for x in pins)


def _effective_matrix(loaded: LoadedProfiles) -> Dict[str, Any]:
    matrix = copy.deepcopy(loaded.board["matrix"])
    overrides = loaded.flash.get("matrix_overrides", {})
    if "rows" in overrides:
        matrix["rows"] = overrides["rows"]
    if "cols" in overrides:
        matrix["cols"] = overrides["cols"]
    return matrix


def _key_token_to_expr(token: str) -> str:
    if token == "KC_NO":
        return "KC_NO"
    if token == "KC_TRANSPARENT":
        return "KC_TRANSPARENT"
    if token.startswith("KC_BASIC(") and token.endswith(")"):
        return token
    if token.startswith("KC_"):
        return f"KC_BASIC({token})"
    raise ValidationError(f"Unsupported key token '{token}' in keymap YAML")


def generate_keymap_header(loaded: LoadedProfiles, output: Path = GENERATED_KEYMAP) -> Path:
    layers = loaded.board["keymap"]["layers"]

    matrix = _effective_matrix(loaded)
    rows = int(matrix["rows"])
    cols = int(matrix["cols"])

    out_lines: List[str] = [
        "// Auto-generated by tools/espkm.py from keymaps YAML; DO NOT EDIT",
        "#pragma once",
        "",
        "#include \"espkm/keycodes.h\"",
        "",
        f"#define ESPKM_GENERATED_LAYERS {len(layers)}",
    ]
    for li, layer in enumerate(layers):
        if not isinstance(layer, dict):
            raise ValidationError(f"board keymap layers[{li}] must be mapping")
        grid = _require(layer, "rows", f"board keymap layers[{li}]")
        if not isinstance(grid, list) or len(grid) != rows:
            raise ValidationError(
                f"board keymap layers[{li}].rows must have {rows} row entries for current board"
            )
        flattened: List[str] = []
        for ri, rowvals in enumerate(grid):
            if not isinstance(rowvals, list) or len(rowvals) != cols:
                raise ValidationError(
                    f"board keymap layers[{li}].rows[{ri}] must contain {cols} key tokens"
                )
            for ci, tok in enumerate(rowvals):
                if not isinstance(tok, str):
                    raise ValidationError(
                        f"board keymap layers[{li}].rows[{ri}][{ci}] must be string token"
                    )
                flattened.append(_key_token_to_expr(tok))
        out_lines.append(f"static const keycode_t generated_layer_{li}[] = {{")
        for start in range(0, len(flattened), cols):
            out_lines.append("    " + ", ".join(flattened[start:start + cols]) + ",")
        out_lines.append("};")
        out_lines.append("")
    out_lines.append("static const keycode_t *generated_layers[] = {")
    for li in range(len(layers)):
        out_lines.append(f"    generated_layer_{li},")
    out_lines.append("};")

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(out_lines) + "\n", encoding="utf-8")
    return output


def generate_sdkconfig_fragment(loaded: LoadedProfiles, output: Path = GENERATED_SDKCONFIG) -> Path:
    matrix = _effective_matrix(loaded)
    rows = matrix["rows"]
    cols = matrix["cols"]
    row_pins = matrix["pins"]["rows"]
    col_pins = matrix["pins"]["cols"]
    if len(row_pins) != rows or len(col_pins) != cols:
        raise ValidationError(
            "Effective matrix dimensions do not match pin list lengths after overrides"
        )

    features = loaded.flash.get("features", {})
    verbose = features.get("verbose_log")
    cli_enable = features.get("cli_enable")

    lines = [
        "# Auto-generated by tools/espkm.py; DO NOT EDIT",
        "CONFIG_ESPKM_DIRECT_MATRIX=y",
        f"CONFIG_ESPKM_MATRIX_ROWS={rows}",
        f"CONFIG_ESPKM_MATRIX_COLS={cols}",
        f'CONFIG_ESPKM_DIRECT_ROW_PINS="{_join_pins(row_pins)}"',
        f'CONFIG_ESPKM_DIRECT_COL_PINS="{_join_pins(col_pins)}"',
    ]
    hardware = loaded.board.get("hardware", {})
    if isinstance(hardware, dict):
        flash_mb = hardware.get("flash_size_mb")
        if isinstance(flash_mb, int):
            lines.append(f'CONFIG_ESPTOOLPY_FLASHSIZE="{flash_mb}MB"')
            for mb in sorted(SUPPORTED_FLASH_MB):
                key = f"CONFIG_ESPTOOLPY_FLASHSIZE_{mb}MB"
                lines.append(f"{key}=y" if mb == flash_mb else f"# {key} is not set")
        psram = hardware.get("psram")
        if isinstance(psram, bool):
            lines.append("CONFIG_SPIRAM=y" if psram else "# CONFIG_SPIRAM is not set")
        partition = hardware.get("partition_table")
        if isinstance(partition, str):
            lines.append(f'CONFIG_PARTITION_TABLE_FILENAME="partitions_{partition}.csv"')

    sdk_overrides = loaded.board.get("sdkconfig_overrides", {})
    if isinstance(sdk_overrides, dict):
        for k, v in sdk_overrides.items():
            if isinstance(v, bool):
                lines.append(f"{k}=y" if v else f"# {k} is not set")
            elif isinstance(v, int):
                lines.append(f"{k}={v}")
            else:
                val = str(v).replace('"', '\\"')
                lines.append(f'{k}="{val}"')

    usb = loaded.board.get("usb", {})
    if isinstance(usb, dict):
        if "vid" in usb:
            lines.append("# CONFIG_TINYUSB_DESC_USE_ESPRESSIF_VID is not set")
            lines.append(f"CONFIG_TINYUSB_DESC_CUSTOM_VID=0x{int(usb['vid']) & 0xFFFF:04x}")
        if "pid" in usb:
            lines.append("# CONFIG_TINYUSB_DESC_USE_DEFAULT_PID is not set")
            lines.append(f"CONFIG_TINYUSB_DESC_CUSTOM_PID=0x{int(usb['pid']) & 0xFFFF:04x}")
        if "manufacturer" in usb:
            m = str(usb["manufacturer"]).replace('"', '\\"')
            lines.append(f'CONFIG_TINYUSB_DESC_MANUFACTURER_STRING="{m}"')
        if "product" in usb:
            p = str(usb["product"]).replace('"', '\\"')
            lines.append(f'CONFIG_TINYUSB_DESC_PRODUCT_STRING="{p}"')
        if "serial" in usb:
            s = str(usb["serial"]).replace('"', '\\"')
            lines.append(f'CONFIG_TINYUSB_DESC_SERIAL_STRING="{s}"')

    ble = loaded.board.get("ble", {})
    if isinstance(ble, dict):
        if "device_name" in ble:
            n = str(ble["device_name"]).replace('"', '\\"')
            lines.append(f'CONFIG_ESPKM_BLE_DEVICE_NAME="{n}"')
        if "manufacturer" in ble:
            m = str(ble["manufacturer"]).replace('"', '\\"')
            lines.append(f'CONFIG_ESPKM_BLE_MANUFACTURER="{m}"')
        if "model" in ble:
            md = str(ble["model"]).replace('"', '\\"')
            lines.append(f'CONFIG_ESPKM_BLE_MODEL="{md}"')
    if isinstance(verbose, bool):
        lines.append("CONFIG_ESPKM_VERBOSE_LOG=y" if verbose else "# CONFIG_ESPKM_VERBOSE_LOG is not set")
    if isinstance(cli_enable, bool):
        lines.append("CONFIG_ESPKM_CLI_ENABLE=y" if cli_enable else "# CONFIG_ESPKM_CLI_ENABLE is not set")

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return output


def _run_idf(args: List[str], loaded: LoadedProfiles, dry_run: bool) -> int:
    generated = generate_sdkconfig_fragment(loaded)
    defaults = f"sdkconfig.defaults;{generated.as_posix()}"
    env = os.environ.copy()
    env["SDKCONFIG_DEFAULTS"] = defaults

    cmd = ["idf.py"] + args
    print(f"[espkm] SDKCONFIG_DEFAULTS={defaults}")
    print("[espkm] Running:", " ".join(cmd))
    if dry_run:
        return 0
    proc = subprocess.run(cmd, cwd=str(ROOT), env=env, check=False)
    return proc.returncode


def _save_yaml(path: Path, data: Dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        yaml.safe_dump(data, f, sort_keys=False)


def _list_profile_names(directory: Path) -> List[str]:
    return sorted(p.stem for p in directory.glob("*.yaml"))


def _pick_from_list(title: str, choices: List[str]) -> str | None:
    if not choices:
        print(f"No options found for {title}.")
        return None
    print(f"\n{title}")
    for idx, choice in enumerate(choices, start=1):
        print(f"  {idx}. {choice}")
    raw = input("Select number (blank to cancel): ").strip()
    if not raw:
        return None
    if not raw.isdigit():
        print("Invalid selection.")
        return None
    sel = int(raw)
    if sel < 1 or sel > len(choices):
        print("Selection out of range.")
        return None
    return choices[sel - 1]


def interactive_shell() -> int:
    print("ESPKM Interactive CLI")
    print("Use this menu to select board/language, validate, generate, flash, and monitor.\n")
    while True:
        try:
            loaded, warnings = validate_all()
            active_board = loaded.board_name
            active_lang = loaded.language_name
            target = loaded.flash.get("build", {}).get("target", "?")
            serial = loaded.flash.get("serial", {})
            port = serial.get("port", "")
            baud = serial.get("baud", "")
            print(f"Active board={active_board} language={active_lang} target={target} port={port} baud={baud}")
            if warnings:
                for w in warnings:
                    print(f"WARNING: {w}")
        except ValidationError as e:
            print(f"Current config has errors: {e}")

        print("\nActions")
        print("  1. Select board")
        print("  2. Select language")
        print("  3. Set serial port")
        print("  4. Set serial baud")
        print("  5. Validate config")
        print("  6. Generate sdkconfig bridge")
        print("  7. Flash")
        print("  8. Monitor")
        print("  9. Show active config")
        print("  0. Exit")
        action = input("Choose action: ").strip()

        if action == "0":
            print("Bye.")
            return 0
        if action == "1":
            picked = _pick_from_list("Available boards", _list_profile_names(BOARDS_DIR))
            if picked:
                ns = argparse.Namespace(action="use", name=picked)
                cmd_board(ns)
            continue
        if action == "2":
            picked = _pick_from_list("Available languages", _list_profile_names(LANGUAGES_DIR))
            if picked:
                ns = argparse.Namespace(action="use", name=picked)
                cmd_lang(ns)
            continue
        if action == "3":
            new_port = input("Enter serial port (e.g., COM7): ").strip()
            if new_port:
                ns = argparse.Namespace(action="set", kv=f"serial.port={new_port}")
                cmd_config(ns)
            continue
        if action == "4":
            new_baud = input("Enter serial baud (e.g., 460800): ").strip()
            if new_baud:
                ns = argparse.Namespace(action="set", kv=f"serial.baud={new_baud}")
                cmd_config(ns)
            continue
        if action == "5":
            ns = argparse.Namespace(action="validate")
            cmd_config(ns)
            continue
        if action == "6":
            ns = argparse.Namespace(action="generate")
            cmd_config(ns)
            continue
        if action == "7":
            dry = input("Dry-run only? [Y/n]: ").strip().lower()
            ns = argparse.Namespace(dry_run=(dry in {"", "y", "yes"}))
            cmd_flash(ns)
            continue
        if action == "8":
            dry = input("Dry-run only? [Y/n]: ").strip().lower()
            ns = argparse.Namespace(dry_run=(dry in {"", "y", "yes"}))
            cmd_monitor(ns)
            continue
        if action == "9":
            ns = argparse.Namespace(action="print")
            cmd_config(ns)
            continue
        print("Unknown action.")


def cmd_board(args: argparse.Namespace) -> int:
    if args.action == "list":
        for p in sorted(BOARDS_DIR.glob("*.yaml")):
            print(p.stem)
        return 0

    loaded, _ = validate_all()
    if args.action == "show":
        if args.name:
            data = _load_yaml(BOARDS_DIR / f"{args.name}.yaml")
            _validate_board(args.name, data)
            print(yaml.safe_dump(data, sort_keys=False))
            return 0
        print(yaml.safe_dump(loaded.board, sort_keys=False))
        return 0

    if args.action == "use":
        flash = _load_yaml(FLASH_CONFIG_PATH)
        _load_yaml(BOARDS_DIR / f"{args.name}.yaml")
        flash["board"] = args.name
        _save_yaml(FLASH_CONFIG_PATH, flash)
        print(f"Updated config/flash.yaml board -> {args.name}")
        return 0

    return 1


def cmd_lang(args: argparse.Namespace) -> int:
    if args.action == "list":
        for p in sorted(LANGUAGES_DIR.glob("*.yaml")):
            print(p.stem)
        return 0

    loaded, _ = validate_all()
    if args.action == "show":
        if args.name:
            data = _load_yaml(LANGUAGES_DIR / f"{args.name}.yaml")
            _validate_language(args.name, data)
            print(yaml.safe_dump(data, sort_keys=False))
            return 0
        print(yaml.safe_dump(loaded.language, sort_keys=False))
        return 0

    if args.action == "use":
        flash = _load_yaml(FLASH_CONFIG_PATH)
        _load_yaml(LANGUAGES_DIR / f"{args.name}.yaml")
        flash["language"] = args.name
        _save_yaml(FLASH_CONFIG_PATH, flash)
        print(f"Updated config/flash.yaml language -> {args.name}")
        return 0

    return 1


def cmd_config(args: argparse.Namespace) -> int:
    if args.action == "print":
        flash = _load_yaml(FLASH_CONFIG_PATH)
        print(yaml.safe_dump(flash, sort_keys=False))
        return 0

    if args.action == "set":
        if "=" not in args.kv:
            raise ValidationError("config set expects key=value")
        key, raw = args.kv.split("=", 1)
        key = key.strip()
        raw = raw.strip()
        if not key:
            raise ValidationError("config set key cannot be empty")

        try:
            value = yaml.safe_load(raw)
        except yaml.YAMLError:
            value = raw

        flash = _load_yaml(FLASH_CONFIG_PATH)
        parts = key.split(".")
        cur = flash
        for p in parts[:-1]:
            if p not in cur or not isinstance(cur[p], dict):
                cur[p] = {}
            cur = cur[p]
        cur[parts[-1]] = value
        _save_yaml(FLASH_CONFIG_PATH, flash)
        print(f"Updated config/flash.yaml: {key}={value!r}")
        return 0

    loaded, warnings = validate_all()
    if args.action == "validate":
        print("Validation OK")
        print(f"- board: {loaded.board_name}")
        print(f"- language: {loaded.language_name}")
        print(f"- target: {loaded.flash['build']['target']}")
        if warnings:
            for w in warnings:
                print(f"WARNING: {w}")
        return 0

    if args.action == "generate":
        out = generate_sdkconfig_fragment(loaded)
        kmap = generate_keymap_header(loaded)
        print(f"Generated {out}")
        print(f"Generated {kmap}")
        if warnings:
            for w in warnings:
                print(f"WARNING: {w}")
        return 0

    return 1


def cmd_flash(args: argparse.Namespace) -> int:
    loaded, warnings = validate_all()
    if warnings:
        for w in warnings:
            print(f"WARNING: {w}")

    flash = loaded.flash
    serial = flash.get("serial", {})
    if not isinstance(serial, dict):
        raise ValidationError("config/flash.yaml.serial must be mapping")

    cmd = ["set-target", flash["build"]["target"], "flash"]
    port = serial.get("port")
    baud = serial.get("baud")
    if port:
        cmd = ["-p", str(port)] + cmd
    else:
        raise ValidationError("Missing serial.port in config/flash.yaml for flash command")
    if baud:
        cmd = ["-b", str(baud)] + cmd
    return _run_idf(cmd, loaded, dry_run=args.dry_run)


def cmd_monitor(args: argparse.Namespace) -> int:
    loaded, warnings = validate_all()
    if warnings:
        for w in warnings:
            print(f"WARNING: {w}")

    serial = loaded.flash.get("serial", {})
    if not isinstance(serial, dict):
        raise ValidationError("config/flash.yaml.serial must be mapping")
    port = serial.get("port")
    if not port:
        raise ValidationError("Missing serial.port in config/flash.yaml for monitor command")
    cmd = ["-p", str(port), "monitor"]
    return _run_idf(cmd, loaded, dry_run=args.dry_run)


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="espkm", description="ESPKM profile/config CLI")
    sub = p.add_subparsers(dest="cmd", required=False)

    b = sub.add_parser("board", help="Board profile commands")
    b_sub = b.add_subparsers(dest="action", required=True)
    b_sub.add_parser("list", help="List board profiles")
    b_show = b_sub.add_parser("show", help="Show board profile")
    b_show.add_argument("name", nargs="?", default=None, help="Board profile name (default active)")
    b_use = b_sub.add_parser("use", help="Select board profile")
    b_use.add_argument("name", help="Board profile name")
    b.set_defaults(func=cmd_board)

    l = sub.add_parser("lang", help="Language preset commands")
    l_sub = l.add_subparsers(dest="action", required=True)
    l_sub.add_parser("list", help="List language presets")
    l_show = l_sub.add_parser("show", help="Show language preset")
    l_show.add_argument("name", nargs="?", default=None, help="Language preset name (default active)")
    l_use = l_sub.add_parser("use", help="Select language preset")
    l_use.add_argument("name", help="Language preset name")
    l.set_defaults(func=cmd_lang)

    c = sub.add_parser("config", help="Config commands")
    c_sub = c.add_subparsers(dest="action", required=True)
    c_sub.add_parser("validate", help="Validate flash/board/lang config")
    c_sub.add_parser("print", help="Print active flash config")
    c_set = c_sub.add_parser("set", help="Set config key")
    c_set.add_argument("kv", help="key=value, key supports dotted paths")
    c_sub.add_parser("generate", help="Generate build bridge sdkconfig fragment")
    c.set_defaults(func=cmd_config)

    f = sub.add_parser("flash", help="Run idf.py flash from selected config")
    f.add_argument("--dry-run", action="store_true", help="Show command without executing")
    f.set_defaults(func=cmd_flash)

    m = sub.add_parser("monitor", help="Run idf.py monitor from selected config")
    m.add_argument("--dry-run", action="store_true", help="Show command without executing")
    m.set_defaults(func=cmd_monitor)

    i = sub.add_parser("interactive", help="Run interactive menu")
    i.set_defaults(func=lambda _args: interactive_shell())

    return p


def main(argv: List[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if not getattr(args, "cmd", None):
        return interactive_shell()
    try:
        return int(args.func(args))
    except ValidationError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2
    except FileNotFoundError as e:
        print(f"ERROR: command failed, missing executable: {e}", file=sys.stderr)
        return 127


if __name__ == "__main__":
    raise SystemExit(main())
