#include "keyboard.h"

#include "espkm/combos.h"
#include "espkm/keycodes.h"

#include "generated_keymap.h"

// Fallback map used if generated map is absent/mismatched.
static const keycode_t fallback_layer0[] = {
    KC_BASIC(KC_Q), KC_BASIC(KC_W), KC_BASIC(KC_E), KC_BASIC(KC_A), KC_BASIC(KC_S), KC_BASIC(KC_D),
};

keycode_t espkm_keymap_get(uint8_t layer, uint8_t row, uint8_t col) {
  uint8_t cols = espkm_keyboard_cols();
  uint32_t idx = (uint32_t)row * cols + col;

  if (layer < ESPKM_GENERATED_LAYERS) {
    const keycode_t *layer_data = generated_layers[layer];
    uint32_t cap = (uint32_t)espkm_keyboard_rows() * cols;
    if (idx < cap) {
      return layer_data[idx];
    }
    return KC_NO;
  }

  if (layer == 0 && idx < (sizeof(fallback_layer0) / sizeof(fallback_layer0[0]))) {
    return fallback_layer0[idx];
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
