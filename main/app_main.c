#include "esp_log.h"
#include "nvs_flash.h"

#include "espkm/core.h"
#include "espkm/matrix.h"
#include "espkm/router.h"
#include "espkm/usb.h"
#include "espkm/ble.h"
#include "espkm/macros.h"
#include "espkm/cli.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "sdkconfig.h"

static const char *TAG = "app";

void app_main(void) {
  ESP_LOGI(TAG, "Starting espkm");

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
    ESP_ERROR_CHECK(ret);
  }

  QueueHandle_t event_q = xQueueCreate(CONFIG_ESPKM_EVENT_QUEUE_LEN, sizeof(KeyEvent));
  QueueHandle_t report_q = xQueueCreate(CONFIG_ESPKM_REPORT_QUEUE_LEN, sizeof(HidReport));
  if (event_q == NULL || report_q == NULL) {
    ESP_LOGE(TAG, "failed to create queues event=%p report=%p", event_q, report_q);
    return;
  }

  espkm_matrix_start(event_q);
  espkm_router_start(report_q);
  espkm_usb_start();
  espkm_ble_start();
  espkm_macro_start(event_q);
  espkm_core_start(event_q, report_q);

  espkm_cli_start();
}
