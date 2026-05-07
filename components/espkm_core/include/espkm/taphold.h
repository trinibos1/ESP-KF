#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "espkm/core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ESPKM_TAPHOLD_P1_INTERRUPT_HOLD = 1,
  ESPKM_TAPHOLD_P2_PERMISSIVE = 2,
} espkm_taphold_policy_t;

typedef struct {
  bool active;
  bool decided;
  bool is_hold;
  bool interrupted;
  bool released;
  uint32_t press_time;
  uint32_t tap_term_ms;
  // pending semantics:
  keycode_t tap_kc;
  uint8_t hold_mods;
  uint8_t hold_layer;
  bool hold_is_layer;
  bool hold_used; // set if hold behavior actually applied (for retro_tap style policies)
} TapHoldState;

typedef struct {
  espkm_taphold_policy_t policy;
  bool p2_permissive_hold;
  bool p2_ignore_mod_tap_interrupt;
  bool p2_retro_tap;
  uint32_t tapping_term_ms;
} TapHoldConfig;

void espkm_taphold_init(TapHoldConfig *cfg);

// Called for each key event before layer/keycode translation. This stage may:
// - consume the event (return true) and emit derived actions via callbacks
// - or pass through (return false).
bool espkm_taphold_process_event(const TapHoldConfig *cfg,
                                TapHoldState *states,
                                uint32_t states_len,
                                const KeyEvent *ev,
                                espkm_emit_action_fn emit,
                                void *emit_ctx);

// Advance time-based decisions (timeouts) using current time.
void espkm_taphold_tick(const TapHoldConfig *cfg,
                        TapHoldState *states,
                        uint32_t states_len,
                        uint32_t now_ms,
                        espkm_emit_action_fn emit,
                        void *emit_ctx);

// Arms tap/hold state for a specific physical key position when its resolved keycode is tap/hold.
bool espkm_taphold_arm_key(const TapHoldConfig *cfg,
                           TapHoldState *states,
                           uint32_t states_len,
                           uint8_t row,
                           uint8_t col,
                           keycode_t kc,
                           uint32_t now_ms);

// Handles release (and any pending decision) for a previously armed tap/hold key position.
bool espkm_taphold_release_key(const TapHoldConfig *cfg,
                               TapHoldState *states,
                               uint32_t states_len,
                               uint8_t row,
                               uint8_t col,
                               uint32_t now_ms,
                               espkm_emit_action_fn emit,
                               void *emit_ctx);

#ifdef __cplusplus
}
#endif
