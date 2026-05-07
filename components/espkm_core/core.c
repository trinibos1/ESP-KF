#include "espkm/core.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "sdkconfig.h"

#include "espkm/combos.h"
#include "espkm/debug_ring.h"
#include "espkm/keycodes.h"
#include "espkm/layers.h"
#include "espkm/report_builder.h"
#include "espkm/taphold.h"
#include "espkm/stats.h"

static const char *TAG = "espkm_core";

static QueueHandle_t s_event_q;
static QueueHandle_t s_report_q;

static void enqueue_report_overwrite(QueueHandle_t q, const HidReport *r) {
  if (!q) {
    return;
  }
  if (uxQueueMessagesWaiting(q) >= (UBaseType_t)CONFIG_ESPKM_REPORT_QUEUE_LEN) {
    // Overwrite semantics: pop one then push.
    HidReport tmp;
    (void)xQueueReceive(q, &tmp, 0);
    espkm_stats_report_overwrite();
  }
  (void)xQueueSend(q, r, 0);
}

typedef struct {
  LayerState layers;
  TapHoldConfig taphold_cfg;
  // For now: fixed-size taphold states array for up to 8x16.
  TapHoldState taphold_states[128];
  ReportBuilder report;
} CoreState;

static void emit_action_to_report(void *ctx, keycode_t kc, bool pressed) {
  CoreState *st = (CoreState *)ctx;
  // Keycode→Action (minimal v1): basic key and modifiers.
  uint16_t type = kc & KC_TYPE_MASK;
  if (kc == KC_NO || kc == KC_TRANSPARENT) {
    return;
  }

  if (type == KC_TYPE_BASIC) {
    uint8_t usage = (uint8_t)(kc & 0x00FF);
    espkm_report_apply_key(&st->report, usage, pressed);
    espkm_dbg_log(ESPKM_DBG_EV_ACTION, usage, pressed ? 1 : 0, 0);
    return;
  }

  if (type == KC_TYPE_MOD) {
    uint8_t mods = (uint8_t)((kc >> 8) & 0x00FF);
    uint8_t usage = (uint8_t)(kc & 0x00FF);
    // On press: apply mods then key; on release: key then mods.
    if (pressed) {
      espkm_report_apply_mods(&st->report, mods, true);
      espkm_report_apply_key(&st->report, usage, true);
    } else {
      espkm_report_apply_key(&st->report, usage, false);
      espkm_report_apply_mods(&st->report, mods, false);
    }
    return;
  }

  if (type == KC_TYPE_LAYER) {
    // Minimal layer ops: only momentary on press/release for now (op=0).
    uint8_t op = (uint8_t)((kc >> 8) & 0x000F);
    uint8_t layer = (uint8_t)(kc & 0x00FF);
    if (op == 0) {
      if (pressed) {
        espkm_layer_on(&st->layers, layer);
      } else {
        espkm_layer_off(&st->layers, layer);
      }
    }
    return;
  }

  // TODO: KC_TYPE_TAPHOLD and KC_TYPE_MACRO
}

static bool is_undecided_taphold(void *ctx, uint8_t row, uint8_t col) {
  CoreState *st = (CoreState *)ctx;
  uint32_t idx = (uint32_t)row * 16u + (uint32_t)col;
  if (idx >= 128) {
    return false;
  }
  TapHoldState *s = &st->taphold_states[idx];
  return s->active && !s->decided && !s->released;
}

static void core_task(void *arg) {
  (void)arg;
  // Allocate CoreState statically to avoid blowing the task stack
  // (it contains ~4 KB of tap/hold states).
  static CoreState st;
  memset(&st, 0, sizeof(st));
  espkm_layers_init(&st.layers);
  espkm_taphold_init(&st.taphold_cfg);
  espkm_report_init(&st.report);
  espkm_dbg_init();
  espkm_stats_init();
  espkm_stats_start_logger();

  ESP_LOGI(TAG, "core_task started evt_q=%p rpt_q=%p", s_event_q, s_report_q);

  for (;;) {
    KeyEvent ev;
    if (xQueueReceive(s_event_q, &ev, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    espkm_dbg_log(ESPKM_DBG_EV_CORE_IN, ((uint16_t)ev.row << 8) | ev.col, ev.pressed ? 1 : 0, 0);

    // 1) preprocess: (no-op)

    // 2) tap/hold stage (interrupt tracking / future full impl)
    (void)espkm_taphold_process_event(&st.taphold_cfg, st.taphold_states, 128, &ev, emit_action_to_report, &st);
    espkm_taphold_tick(&st.taphold_cfg, st.taphold_states, 128, ev.t, emit_action_to_report, &st);

    // 3) combos stage
    bool combo_consumed = espkm_combos_process(&ev, CONFIG_ESPKM_COMBO_TERM_MS, is_undecided_taphold, &st,
                                              emit_action_to_report, &st);
    if (combo_consumed) {
      // 6) report builder dirty check + 7) enqueue
      HidReport rep;
      if (espkm_report_snapshot_if_dirty(&st.report, &rep)) {
        enqueue_report_overwrite(s_report_q, &rep);
        espkm_dbg_log(ESPKM_DBG_EV_REPORT, rep.modifiers, rep.keys[0], rep.keys[1]);
      }
      continue;
    }

    // 4) layer resolver -> keycode
    keycode_t kc = espkm_resolve_keycode(&st.layers, ev.row, ev.col);

    // 5) keycode -> action(s), including tap/hold.
    if ((kc & KC_TYPE_MASK) == KC_TYPE_TAPHOLD) {
      if (ev.pressed) {
        (void)espkm_taphold_arm_key(&st.taphold_cfg, st.taphold_states, 128, ev.row, ev.col, kc, ev.t);
      } else {
        (void)espkm_taphold_release_key(&st.taphold_cfg, st.taphold_states, 128, ev.row, ev.col, ev.t,
                                        emit_action_to_report, &st);
      }
    } else {
      emit_action_to_report(&st, kc, ev.pressed);
    }

    // 6/7) report builder dirty + enqueue
    HidReport rep;
    if (espkm_report_snapshot_if_dirty(&st.report, &rep)) {
      enqueue_report_overwrite(s_report_q, &rep);
#if CONFIG_ESPKM_VERBOSE_LOG
      ESP_LOGI(TAG, "report mods=0x%02x keys=%02x %02x %02x %02x %02x %02x", rep.modifiers, rep.keys[0], rep.keys[1],
               rep.keys[2], rep.keys[3], rep.keys[4], rep.keys[5]);
#endif
      espkm_dbg_log(ESPKM_DBG_EV_REPORT, rep.modifiers, rep.keys[0], rep.keys[1]);
    }
  }
}

void espkm_core_start(void *event_queue, void *report_queue) {
  s_event_q = (QueueHandle_t)event_queue;
  s_report_q = (QueueHandle_t)report_queue;

  xTaskCreate(core_task, "core_task", CONFIG_ESPKM_CORE_TASK_STACK / sizeof(StackType_t), NULL, CONFIG_ESPKM_CORE_TASK_PRIO,
              NULL);
}
