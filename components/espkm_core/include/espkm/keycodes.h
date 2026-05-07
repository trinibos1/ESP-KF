#pragma once

#include <stdint.h>

#include "espkm/core.h"

#ifdef __cplusplus
extern "C" {
#endif

// Special keycodes (must be stable across layers).
enum {
  KC_NO = 0x0000,
  KC_TRANSPARENT = 0x0001,
};

// Encoded keycode types (high nibble in bits 15..12)
#define KC_TYPE_MASK 0xF000
#define KC_TYPE_BASIC 0x0000
#define KC_TYPE_LAYER 0x1000
#define KC_TYPE_MOD 0x2000
#define KC_TYPE_MACRO 0x3000
#define KC_TYPE_TAPHOLD 0x4000

#define KC_BASIC(k) (keycode_t)(KC_TYPE_BASIC | ((k)&0x0FFF))
#define KC_LAYER_OP(op, v) (keycode_t)(KC_TYPE_LAYER | (((op)&0x000F) << 8) | ((v)&0x00FF))
#define KC_MODDED(mod, k) (keycode_t)(KC_TYPE_MOD | (((mod)&0x00FF) << 8) | ((k)&0x00FF))
#define KC_MACRO(id) (keycode_t)(KC_TYPE_MACRO | ((id)&0x0FFF))

// Tap/Hold composites (LT/MT equivalents)
//
// Encoding for v1 (still uint16_t, QMK-style):
// - bits 15..12: type (0x4)
// - bits 11..8 : argument (layer id 0..15 OR mod index 0..15)
// - bits 7..0  : basic HID usage (0..255)
//
// Subtype is implied by which macro was used at compile-time.
// For MT we use a 4-bit "mod index" that maps to an 8-bit modifier mask.
#define KC_LT(layer_id_0_15, basic_hid_0_255) \
  (keycode_t)(KC_TYPE_TAPHOLD | (((layer_id_0_15)&0x0F) << 8) | ((basic_hid_0_255)&0xFF))
#define KC_MT(mod_index_0_15, basic_hid_0_255) \
  (keycode_t)(KC_TYPE_TAPHOLD | 0x0800 | (((mod_index_0_15)&0x0F) << 8) | ((basic_hid_0_255)&0xFF))

uint8_t espkm_mod_index_mask(uint8_t mod_index);

// Minimal HID usage IDs for keyboard (basic range).
// These match standard USB HID Keyboard/Keypad usage IDs.
enum {
  KC_A = 0x0004,
  KC_B = 0x0005,
  KC_C = 0x0006,
  KC_D = 0x0007,
  KC_E = 0x0008,
  KC_F = 0x0009,
  KC_G = 0x000A,
  KC_H = 0x000B,
  KC_I = 0x000C,
  KC_J = 0x000D,
  KC_K = 0x000E,
  KC_L = 0x000F,
  KC_M = 0x0010,
  KC_N = 0x0011,
  KC_O = 0x0012,
  KC_P = 0x0013,
  KC_Q = 0x0014,
  KC_R = 0x0015,
  KC_S = 0x0016,
  KC_T = 0x0017,
  KC_U = 0x0018,
  KC_V = 0x0019,
  KC_W = 0x001A,
  KC_X = 0x001B,
  KC_Y = 0x001C,
  KC_Z = 0x001D,
  KC_1 = 0x001E,
  KC_2 = 0x001F,
  KC_3 = 0x0020,
  KC_4 = 0x0021,
  KC_5 = 0x0022,
  KC_6 = 0x0023,
  KC_7 = 0x0024,
  KC_8 = 0x0025,
  KC_9 = 0x0026,
  KC_0 = 0x0027,
  KC_ENTER = 0x0028,
  KC_ESC = 0x0029,
  KC_BSPC = 0x002A,
  KC_TAB = 0x002B,
  KC_SPACE = 0x002C,
};

// Modifier bits in HID report (byte 0)
enum {
  MOD_LCTRL = 0x01,
  MOD_LSHIFT = 0x02,
  MOD_LALT = 0x04,
  MOD_LGUI = 0x08,
  MOD_RCTRL = 0x10,
  MOD_RSHIFT = 0x20,
  MOD_RALT = 0x40,
  MOD_RGUI = 0x80,
};

const char *espkm_keycode_name(keycode_t kc);

#ifdef __cplusplus
}
#endif
