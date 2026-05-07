#pragma once

#include <stdint.h>

#include "espkm/core.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t espkm_keyboard_rows(void);
uint8_t espkm_keyboard_cols(void);
keycode_t espkm_keymap_get(uint8_t layer, uint8_t row, uint8_t col);

#ifdef __cplusplus
}
#endif

