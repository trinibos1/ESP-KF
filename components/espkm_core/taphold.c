#include "espkm/taphold.h"

#include <string.h>

#include "sdkconfig.h"

#include "espkm/keycodes.h"

void espkm_taphold_init(TapHoldConfig *cfg) {
  memset(cfg, 0, sizeof(*cfg));
#if CONFIG_ESPKM_TAPHOLD_P2_PERMISSIVE
  cfg->policy = ESPKM_TAPHOLD_P2_PERMISSIVE;
  cfg->p2_permissive_hold = CONFIG_ESPKM_P2_PERMISSIVE_HOLD;
  cfg->p2_ignore_mod_tap_interrupt = CONFIG_ESPKM_P2_IGNORE_MOD_TAP_INTERRUPT;
  cfg->p2_retro_tap = CONFIG_ESPKM_P2_RETRO_TAP;
#else
  cfg->policy = ESPKM_TAPHOLD_P1_INTERRUPT_HOLD;
#endif
  cfg->tapping_term_ms = CONFIG_ESPKM_TAPPING_TERM_MS;
}

static bool kc_is_mt(keycode_t kc) {
  return ((kc & KC_TYPE_MASK) == KC_TYPE_TAPHOLD) && ((kc & 0x0800) != 0);
}

static bool kc_is_lt(keycode_t kc) {
  return ((kc & KC_TYPE_MASK) == KC_TYPE_TAPHOLD) && ((kc & 0x0800) == 0);
}

static void decode_mt(keycode_t kc, TapHoldState *s) {
  uint8_t mod_index = (uint8_t)((kc >> 8) & 0x0F);
  uint8_t basic = (uint8_t)(kc & 0xFF);
  s->tap_kc = KC_BASIC(basic);
  s->hold_mods = espkm_mod_index_mask(mod_index);
  s->hold_is_layer = false;
}

static void decode_lt(keycode_t kc, TapHoldState *s) {
  uint8_t layer = (uint8_t)((kc >> 8) & 0x0F);
  uint8_t basic = (uint8_t)(kc & 0xFF);
  s->tap_kc = KC_BASIC(basic);
  s->hold_layer = layer;
  s->hold_is_layer = true;
}

static uint32_t idx_for(uint8_t row, uint8_t col, uint32_t states_len) {
  uint32_t idx = (uint32_t)row * 16u + (uint32_t)col;
  return idx < states_len ? idx : (states_len - 1);
}

static void decide_hold(const TapHoldConfig *cfg, TapHoldState *s, espkm_emit_action_fn emit, void *emit_ctx) {
  if (s->decided) {
    return;
  }
  s->decided = true;
  s->is_hold = true;
  if (s->hold_is_layer) {
    // Encode layer op as KC_LAYER_OP(op=0 momentary, layer)
    emit(emit_ctx, KC_LAYER_OP(0, s->hold_layer), true);
  } else {
    // Apply mods as a "mod press" action via KC_MODDED(mods, 0).
    // We use KC_MODDED with usage=0 to mean "mods only".
    emit(emit_ctx, (keycode_t)(KC_TYPE_MOD | ((uint16_t)s->hold_mods << 8) | 0x00), true);
  }
  s->hold_used = true;
  (void)cfg;
}

static void decide_tap(const TapHoldConfig *cfg, TapHoldState *s, espkm_emit_action_fn emit, void *emit_ctx) {
  if (s->decided) {
    return;
  }
  s->decided = true;
  s->is_hold = false;
  // Tap = press+release tap_kc
  emit(emit_ctx, s->tap_kc, true);
  emit(emit_ctx, s->tap_kc, false);
  (void)cfg;
}

bool espkm_taphold_process_event(const TapHoldConfig *cfg,
                                TapHoldState *states,
                                uint32_t states_len,
                                const KeyEvent *ev,
                                espkm_emit_action_fn emit,
                                void *emit_ctx) {
  if (!states || states_len == 0) {
    return false;
  }

  // Stage-level interrupt semantics:
  // On any other key press, mark all active undecided tap/hold keys interrupted.
  if (ev->pressed) {
    uint32_t self_idx = idx_for(ev->row, ev->col, states_len);
    for (uint32_t i = 0; i < states_len; i++) {
      if (i == self_idx) {
        continue;
      }
      TapHoldState *s = &states[i];
      if (s->active && !s->decided && !s->released) {
        s->interrupted = true;
        if (cfg->policy == ESPKM_TAPHOLD_P1_INTERRUPT_HOLD) {
          decide_hold(cfg, s, emit, emit_ctx);
        }
      }
    }
  }
  return false;
}

void espkm_taphold_tick(const TapHoldConfig *cfg,
                        TapHoldState *states,
                        uint32_t states_len,
                        uint32_t now_ms,
                        espkm_emit_action_fn emit,
                        void *emit_ctx) {
  if (!states || states_len == 0) {
    return;
  }
  for (uint32_t i = 0; i < states_len; i++) {
    TapHoldState *s = &states[i];
    if (!s->active || s->decided || s->released) {
      continue;
    }
    uint32_t dt = now_ms - s->press_time;
    if (dt >= s->tap_term_ms) {
      decide_hold(cfg, s, emit, emit_ctx);
    }
  }
}

bool espkm_taphold_arm_key(const TapHoldConfig *cfg,
                           TapHoldState *states,
                           uint32_t states_len,
                           uint8_t row,
                           uint8_t col,
                           keycode_t kc,
                           uint32_t now_ms) {
  if (!states || states_len == 0) {
    return false;
  }
  if (!kc_is_mt(kc) && !kc_is_lt(kc)) {
    return false;
  }
  TapHoldState *s = &states[idx_for(row, col, states_len)];
  memset(s, 0, sizeof(*s));
  s->active = true;
  s->press_time = now_ms;
  s->tap_term_ms = cfg->tapping_term_ms;
  if (kc_is_mt(kc)) {
    decode_mt(kc, s);
  } else {
    decode_lt(kc, s);
  }
  return true;
}

bool espkm_taphold_release_key(const TapHoldConfig *cfg,
                               TapHoldState *states,
                               uint32_t states_len,
                               uint8_t row,
                               uint8_t col,
                               uint32_t now_ms,
                               espkm_emit_action_fn emit,
                               void *emit_ctx) {
  if (!states || states_len == 0) {
    return false;
  }
  TapHoldState *s = &states[idx_for(row, col, states_len)];
  if (!s->active) {
    return false;
  }
  s->released = true;

  if (!s->decided) {
    // Decide on release depending on policy and whether it was interrupted.
    uint32_t dt = now_ms - s->press_time;
    if (cfg->policy == ESPKM_TAPHOLD_P2_PERMISSIVE && cfg->p2_permissive_hold && s->interrupted && dt < s->tap_term_ms) {
      decide_hold(cfg, s, emit, emit_ctx);
    } else {
      decide_tap(cfg, s, emit, emit_ctx);
    }
  } else if (s->is_hold) {
    // Release hold behavior.
    if (s->hold_is_layer) {
      emit(emit_ctx, KC_LAYER_OP(0, s->hold_layer), false);
    } else {
      emit(emit_ctx, (keycode_t)(KC_TYPE_MOD | ((uint16_t)s->hold_mods << 8) | 0x00), false);
    }
  }

  // Optional retro_tap (only meaningful for permissive policy):
  if (cfg->policy == ESPKM_TAPHOLD_P2_PERMISSIVE && cfg->p2_retro_tap && s->is_hold && !s->hold_used) {
    decide_tap(cfg, s, emit, emit_ctx);
  }

  s->active = false;
  return true;
}
