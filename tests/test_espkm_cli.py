import tempfile
import unittest
from pathlib import Path

import yaml

import tools.espkm as espkm


class EspkmCliTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / "config").mkdir()
        (self.root / "boards").mkdir()
        (self.root / "languages").mkdir()
        (self.root / "keyboards" / "demo_macropad" / "keymaps" / "default").mkdir(parents=True)

        self.flash = self.root / "config" / "flash.yaml"
        self.board = self.root / "boards" / "demo.yaml"
        self.lang = self.root / "languages" / "en.yaml"

        self.flash.write_text(
            yaml.safe_dump(
                {
                    "project": "espkm",
                    "board": "demo",
                    "language": "en",
                    "build": {"target": "esp32s3"},
                    "serial": {"port": "COM7", "baud": 460800},
                    "matrix_overrides": {},
                    "features": {"verbose_log": True, "cli_enable": True},
                    "keymap_hints": {"host_layout": "us-qwerty"},
                },
                sort_keys=False,
            ),
            encoding="utf-8",
        )
        self.board.write_text(
            yaml.safe_dump(
                {
                    "name": "Demo",
                    "target": "esp32s3",
                    "matrix": {
                        "rows": 2,
                        "cols": 3,
                        "pins": {"rows": [1, 2], "cols": [3, 4, 5]},
                    },
                    "hardware": {
                        "flash_size_mb": 8,
                        "psram": True,
                        "partition_table": "default_8MB",
                    },
                    "usb": {
                        "vid": 0x303A,
                        "pid": 0x4001,
                        "manufacturer": "EFK",
                        "product": "Test Keyboard",
                        "serial": "TEST-001",
                    },
                    "ble": {
                        "device_name": "test-espkm",
                        "manufacturer": "EFK",
                        "model": "test-model",
                    },
                    "keymap": {
                        "layers": [{"id": 0, "rows": [["KC_Q", "KC_W", "KC_E"], ["KC_A", "KC_S", "KC_D"]]}]
                    },
                    "sdkconfig_overrides": {"CONFIG_BT_NIMBLE_MAX_CONNECTIONS": 2},
                },
                sort_keys=False,
            ),
            encoding="utf-8",
        )
        self.lang.write_text(
            yaml.safe_dump(
                {
                    "name": "English",
                    "host_layout": "us-qwerty",
                    "aliases": {"quote": "KC_BASIC(KC_QUOTE)"},
                },
                sort_keys=False,
            ),
            encoding="utf-8",
        )

        self.orig_root = espkm.ROOT
        self.orig_cfg = espkm.CONFIG_DIR
        self.orig_flash = espkm.FLASH_CONFIG_PATH
        self.orig_boards = espkm.BOARDS_DIR
        self.orig_langs = espkm.LANGUAGES_DIR
        self.orig_gen_dir = espkm.GENERATED_DIR
        self.orig_gen_cfg = espkm.GENERATED_SDKCONFIG
        self.orig_generated_keymap = espkm.GENERATED_KEYMAP

        espkm.ROOT = self.root
        espkm.CONFIG_DIR = self.root / "config"
        espkm.FLASH_CONFIG_PATH = self.flash
        espkm.BOARDS_DIR = self.root / "boards"
        espkm.LANGUAGES_DIR = self.root / "languages"
        espkm.GENERATED_DIR = self.root / "config" / "generated"
        espkm.GENERATED_SDKCONFIG = espkm.GENERATED_DIR / "sdkconfig.espkm"
        espkm.GENERATED_KEYMAP = self.root / "keyboards" / "demo_macropad" / "keymaps" / "default" / "generated_keymap.h"

    def tearDown(self):
        espkm.ROOT = self.orig_root
        espkm.CONFIG_DIR = self.orig_cfg
        espkm.FLASH_CONFIG_PATH = self.orig_flash
        espkm.BOARDS_DIR = self.orig_boards
        espkm.LANGUAGES_DIR = self.orig_langs
        espkm.GENERATED_DIR = self.orig_gen_dir
        espkm.GENERATED_SDKCONFIG = self.orig_gen_cfg
        espkm.GENERATED_KEYMAP = self.orig_generated_keymap
        self.tmp.cleanup()

    def test_validate_all_ok(self):
        loaded, warnings = espkm.validate_all()
        self.assertEqual(loaded.board_name, "demo")
        self.assertEqual(loaded.language_name, "en")
        self.assertEqual(warnings, [])

    def test_validate_duplicate_pin_fails(self):
        data = yaml.safe_load(self.board.read_text(encoding="utf-8"))
        data["matrix"]["pins"]["cols"] = [2, 4, 5]
        self.board.write_text(yaml.safe_dump(data, sort_keys=False), encoding="utf-8")
        with self.assertRaises(espkm.ValidationError):
            espkm.validate_all()

    def test_validate_unknown_target_fails(self):
        data = yaml.safe_load(self.board.read_text(encoding="utf-8"))
        data["target"] = "esp8266"
        self.board.write_text(yaml.safe_dump(data, sort_keys=False), encoding="utf-8")
        with self.assertRaises(espkm.ValidationError):
            espkm.validate_all()

    def test_generate_sdkconfig(self):
        loaded, _ = espkm.validate_all()
        out = espkm.generate_sdkconfig_fragment(loaded)
        txt = out.read_text(encoding="utf-8")
        self.assertIn("CONFIG_ESPKM_MATRIX_ROWS=2", txt)
        self.assertIn('CONFIG_ESPKM_DIRECT_COL_PINS="3,4,5"', txt)
        self.assertIn('CONFIG_ESPTOOLPY_FLASHSIZE="8MB"', txt)
        self.assertIn("CONFIG_SPIRAM=y", txt)
        self.assertIn("CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2", txt)
        self.assertIn("CONFIG_TINYUSB_DESC_CUSTOM_VID=0x303a", txt)
        self.assertIn("CONFIG_TINYUSB_DESC_CUSTOM_PID=0x4001", txt)
        self.assertIn('CONFIG_TINYUSB_DESC_PRODUCT_STRING="Test Keyboard"', txt)
        self.assertIn('CONFIG_ESPKM_BLE_DEVICE_NAME="test-espkm"', txt)
        keymap = espkm.generate_keymap_header(loaded)
        km_txt = keymap.read_text(encoding="utf-8")
        self.assertIn("ESPKM_GENERATED_LAYERS 1", km_txt)
        self.assertIn("KC_BASIC(KC_Q)", km_txt)

    def test_keymap_hint_warning(self):
        data = yaml.safe_load(self.flash.read_text(encoding="utf-8"))
        data["keymap_hints"]["host_layout"] = "fr-azerty"
        self.flash.write_text(yaml.safe_dump(data, sort_keys=False), encoding="utf-8")
        _, warnings = espkm.validate_all()
        self.assertTrue(warnings)

    def test_cli_config_set(self):
        rc = espkm.main(["config", "set", "serial.port=COM9"])
        self.assertEqual(rc, 0)
        data = yaml.safe_load(self.flash.read_text(encoding="utf-8"))
        self.assertEqual(data["serial"]["port"], "COM9")

    def test_cli_flash_dry_run_requires_port(self):
        data = yaml.safe_load(self.flash.read_text(encoding="utf-8"))
        data["serial"]["port"] = ""
        self.flash.write_text(yaml.safe_dump(data, sort_keys=False), encoding="utf-8")
        rc = espkm.main(["flash", "--dry-run"])
        self.assertEqual(rc, 2)

    def test_cli_monitor_dry_run_ok(self):
        rc = espkm.main(["monitor", "--dry-run"])
        self.assertEqual(rc, 0)

    def test_validate_hardware_flash_size_fails(self):
        data = yaml.safe_load(self.board.read_text(encoding="utf-8"))
        data["hardware"]["flash_size_mb"] = 7
        self.board.write_text(yaml.safe_dump(data, sort_keys=False), encoding="utf-8")
        with self.assertRaises(espkm.ValidationError):
            espkm.validate_all()

    def test_validate_usb_vid_fails(self):
        data = yaml.safe_load(self.board.read_text(encoding="utf-8"))
        data["usb"]["vid"] = 70000
        self.board.write_text(yaml.safe_dump(data, sort_keys=False), encoding="utf-8")
        with self.assertRaises(espkm.ValidationError):
            espkm.validate_all()


if __name__ == "__main__":
    unittest.main()
