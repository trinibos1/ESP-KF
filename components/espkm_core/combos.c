#include "espkm/combos.h"

#include "sdkconfig.h"

static uint32_t s_last_press_t[8][16]; // small fixed default for demo; real impl should be dynamic

static bool pos_eq(KeyPos a, uint8_t row, uint8_t col) {
  return a.row == row && a.col == col;
}

bool espkm_combos_process(const KeyEvent *ev,
                          uint32_t combo_term_ms,
                          espkm_is_undecided_fn is_undecided,
                          void *is_undecided_ctx,
                          espkm_emit_action_fn emit,
                          void *emit_ctx) {
  if (!ev->pressed) {
    return false;
  }

  if (ev->row < 8 && ev->col < 16) {
    s_last_press_t[ev->row][ev->col] = ev->t;
  }

  uint32_t count = 0;
  const Combo2 *combos = espkm_keyboard_combos(&count);
  if (!combos || count == 0) {
    return false;
  }

  for (uint32_t i = 0; i < count; i++) {
    const Combo2 *c = &combos[i];
    // Combo priority: only if both keys undecided.
    if (!is_undecided || !is_undecided(is_undecided_ctx, c->a.row, c->a.col) ||
        !is_undecided(is_undecided_ctx, c->b.row, c->b.col)) {
      continue;
    }
    // Trigger if this event matches a member and the other was pressed within combo_term.
    KeyPos other = pos_eq(c->a, ev->row, ev->col) ? c->b : c->a;
    if (!pos_eq(c->a, ev->row, ev->col) && !pos_eq(c->b, ev->row, ev->col)) {
      continue;
    }

    uint32_t other_t = 0;
    if (other.row < 8 && other.col < 16) {
      other_t = s_last_press_t[other.row][other.col];
    }
    uint32_t dt = ev->t - other_t;
    if (other_t != 0 && dt <= combo_term_ms) {
      // Emit output tap: press+release.
      emit(emit_ctx, c->output, true);
      emit(emit_ctx, c->output, false);
      return true;
    }
  }

  return false;
}

