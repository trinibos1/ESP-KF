#include "espkm/macros.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

static const char *TAG = "macros";

static QueueHandle_t s_event_q;

static void macro_task(void *arg) {
  (void)arg;
  ESP_LOGI(TAG, "macro_task started (scaffold)");
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    // TODO: consume macroQueue and emit synthetic KeyEvents into s_event_q.
  }
}

void espkm_macro_start(QueueHandle_t event_queue) {
  s_event_q = event_queue;
  (void)s_event_q;
  xTaskCreate(macro_task, "macro_task", 4096 / sizeof(StackType_t), NULL, 6, NULL);
}

