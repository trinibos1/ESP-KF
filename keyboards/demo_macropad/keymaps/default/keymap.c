#include "keyboard.h"

#include "espkm/combos.h"
#include "espkm/keycodes.h"

// 2x3 demo keymap:
// [Q W E]
// [A S D]
static const keycode_t layer0[] = {
    KC_BASIC(KC_Q), KC_BASIC(KC_W), KC_BASIC(KC_E),
    KC_BASIC(KC_A), KC_BASIC(KC_S), KC_BASIC(KC_D),
};


keycode_t espkm_keymap_get(uint8_t layer, uint8_t row, uint8_t col) {
  uint8_t cols = espkm_keyboard_cols();
  uint32_t idx = (uint32_t)row * cols + col;
  if (layer == 0) {
    if (idx < (sizeof(layer0) / sizeof(layer0[0]))) {
      return layer0[idx];
    }
    return KC_NO;
  }
  return KC_TRANSPARENT;
}

// No combos for demo.
const Combo2 *espkm_keyboard_combos(uint32_t *count) {
  if (count) {
    *count = 0;
  }
  return 0;
}
