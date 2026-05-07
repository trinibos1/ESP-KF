#include "espkm/layers.h"

#include "espkm/combos.h"
#include "espkm/keycodes.h"

// Weak defaults so the firmware links even before a keyboard component is selected.
// A keyboard component (e.g. keyboards/demo_macropad) should provide strong definitions.

__attribute__((weak)) uint8_t espkm_keyboard_rows(void) {
  return 0;
}

__attribute__((weak)) uint8_t espkm_keyboard_cols(void) {
  return 0;
}

__attribute__((weak)) keycode_t espkm_keymap_get(uint8_t layer, uint8_t row, uint8_t col) {
  (void)layer;
  (void)row;
  (void)col;
  return KC_NO;
}

__attribute__((weak)) const Combo2 *espkm_keyboard_combos(uint32_t *count) {
  if (count) {
    *count = 0;
  }
  return 0;
}

