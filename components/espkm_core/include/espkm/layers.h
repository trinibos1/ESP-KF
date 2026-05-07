#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "espkm/core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESPKM_MAX_LAYERS 8

typedef struct {
  uint8_t default_layer;
  uint8_t active_mask; // bit i => layer i active (in addition to default_layer)
} LayerState;

void espkm_layers_init(LayerState *s);
void espkm_layer_on(LayerState *s, uint8_t layer);
void espkm_layer_off(LayerState *s, uint8_t layer);
bool espkm_layer_is_on(const LayerState *s, uint8_t layer);
uint8_t espkm_layers_highest_active(const LayerState *s);

// Keyboard-provided mapping functions (implemented by keyboard component).
uint8_t espkm_keyboard_rows(void);
uint8_t espkm_keyboard_cols(void);
keycode_t espkm_keymap_get(uint8_t layer, uint8_t row, uint8_t col);

keycode_t espkm_resolve_keycode(const LayerState *s, uint8_t row, uint8_t col);

#ifdef __cplusplus
}
#endif

