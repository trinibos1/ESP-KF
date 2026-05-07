#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "espkm/core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t row;
  uint8_t col;
} KeyPos;

typedef struct {
  KeyPos a;
  KeyPos b;
  keycode_t output;
} Combo2;

// Keyboard supplies list; can be empty.
const Combo2 *espkm_keyboard_combos(uint32_t *count);

// Combos require tap/hold undecided info; we pass a callback.
typedef bool (*espkm_is_undecided_fn)(void *ctx, uint8_t row, uint8_t col);

// If a combo triggers, returns true and emits output press+release via emit().
bool espkm_combos_process(const KeyEvent *ev,
                          uint32_t combo_term_ms,
                          espkm_is_undecided_fn is_undecided,
                          void *is_undecided_ctx,
                          espkm_emit_action_fn emit,
                          void *emit_ctx);

#ifdef __cplusplus
}
#endif

