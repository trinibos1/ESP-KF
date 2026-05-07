#include "espkm/keycodes.h"

uint8_t espkm_mod_index_mask(uint8_t mod_index) {
  // Keep stable; mod_index is stored in keycodes and persisted in keymaps.
  // 0 => none
  static const uint8_t k_table[16] = {
      0,
      MOD_LCTRL,
      MOD_LSHIFT,
      MOD_LALT,
      MOD_LGUI,
      (uint8_t)(MOD_LCTRL | MOD_LSHIFT),
      (uint8_t)(MOD_LCTRL | MOD_LALT),
      (uint8_t)(MOD_LSHIFT | MOD_LALT),
      MOD_RCTRL,
      MOD_RSHIFT,
      MOD_RALT,
      MOD_RGUI,
      (uint8_t)(MOD_RCTRL | MOD_RSHIFT),
      (uint8_t)(MOD_RCTRL | MOD_RALT),
      (uint8_t)(MOD_RSHIFT | MOD_RALT),
      (uint8_t)(MOD_LCTRL | MOD_LSHIFT | MOD_LALT),
  };
  return k_table[mod_index & 0x0F];
}

const char *espkm_keycode_name(keycode_t kc) {
  switch (kc) {
  case KC_NO:
    return "KC_NO";
  case KC_TRANSPARENT:
    return "KC_TRANSPARENT";
  default:
    return "KC";
  }
}
