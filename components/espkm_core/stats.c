#include "espkm/stats.h"

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "sdkconfig.h"

static const char *TAG = "espkm_stats";

static volatile uint32_t s_matrix_event_drops = 0;
static volatile uint32_t s_event_queue_overflows = 0;
static volatile uint32_t s_report_overwrites = 0;

void espkm_stats_init(void) {
  s_matrix_event_drops = 0;
  s_event_queue_overflows = 0;
  s_report_overwrites = 0;
}

void espkm_stats_matrix_event_drop(void) {
  s_matrix_event_drops++;
}

void espkm_stats_event_queue_overflow(void) {
  s_event_queue_overflows++;
}

void espkm_stats_report_overwrite(void) {
  s_report_overwrites++;
}

espkm_stats_t espkm_stats_snapshot(void) {
  espkm_stats_t s = {
      .matrix_event_drops = s_matrix_event_drops,
      .event_queue_overflows = s_event_queue_overflows,
      .report_overwrites = s_report_overwrites,
  };
  return s;
}

static void stats_task(void *arg) {
  (void)arg;
#if CONFIG_ESPKM_STATS_LOG_PERIOD_MS > 0
  const TickType_t period = pdMS_TO_TICKS(CONFIG_ESPKM_STATS_LOG_PERIOD_MS);
  for (;;) {
    vTaskDelay(period);
    espkm_stats_t s = espkm_stats_snapshot();
    ESP_LOGI(TAG, "drops: matrix=%" PRIu32 " eventq=%" PRIu32 " report_overwrites=%" PRIu32, s.matrix_event_drops,
             s.event_queue_overflows, s.report_overwrites);
  }
#else
  vTaskDelete(NULL);
#endif
}

void espkm_stats_start_logger(void) {
#if CONFIG_ESPKM_STATS_LOG_PERIOD_MS > 0
  xTaskCreate(stats_task, "espkm_stats", 3072 / sizeof(StackType_t), NULL, 3, NULL);
#else
  (void)stats_task;
  (void)TAG;
#endif
}
