#include "espkm/layers.h"

#include "espkm/keycodes.h"

void espkm_layers_init(LayerState *s) {
  s->default_layer = 0;
  s->active_mask = 0;
}

void espkm_layer_on(LayerState *s, uint8_t layer) {
  if (layer >= ESPKM_MAX_LAYERS) {
    return;
  }
  s->active_mask |= (1u << layer);
}

void espkm_layer_off(LayerState *s, uint8_t layer) {
  if (layer >= ESPKM_MAX_LAYERS) {
    return;
  }
  s->active_mask &= (uint8_t)~(1u << layer);
}

bool espkm_layer_is_on(const LayerState *s, uint8_t layer) {
  if (layer >= ESPKM_MAX_LAYERS) {
    return false;
  }
  if (layer == s->default_layer) {
    return true;
  }
  return (s->active_mask & (1u << layer)) != 0;
}

uint8_t espkm_layers_highest_active(const LayerState *s) {
  for (int i = ESPKM_MAX_LAYERS - 1; i >= 0; i--) {
    if (espkm_layer_is_on(s, (uint8_t)i)) {
      return (uint8_t)i;
    }
  }
  return s->default_layer;
}

keycode_t espkm_resolve_keycode(const LayerState *s, uint8_t row, uint8_t col) {
  uint8_t highest = espkm_layers_highest_active(s);
  for (int8_t layer = (int8_t)highest; layer >= 0; layer--) {
    if (!espkm_layer_is_on(s, (uint8_t)layer)) {
      continue;
    }
    keycode_t kc = espkm_keymap_get((uint8_t)layer, row, col);
    if (kc != KC_TRANSPARENT) {
      return kc;
    }
  }
  // Fallback: default layer mapping.
  return espkm_keymap_get(s->default_layer, row, col);
}

