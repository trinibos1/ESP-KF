#include "espkm/router.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"

#include "sdkconfig.h"

#include "espkm/debug_ring.h"

static const char *TAG = "router";

espkm_mailbox_t g_espkm_usb_mailbox;
espkm_mailbox_t g_espkm_ble_mailbox;
volatile espkm_transport_state_t g_espkm_transport_state = ESPKM_TRANSPORT_NONE;

static QueueHandle_t s_report_q;
static SemaphoreHandle_t s_mailbox_mutex = NULL;

void espkm_mailbox_lock(void) {
  if (s_mailbox_mutex) {
    xSemaphoreTake(s_mailbox_mutex, portMAX_DELAY);
  }
}

void espkm_mailbox_unlock(void) {
  if (s_mailbox_mutex) {
    xSemaphoreGive(s_mailbox_mutex);
  }
}

// Sender tasks provide these status hooks.
bool espkm_usb_is_enumerated(void);
bool espkm_ble_is_connected(void);

static void router_task(void *arg) {
  (void)arg;
  memset(&g_espkm_usb_mailbox, 0, sizeof(g_espkm_usb_mailbox));
  memset(&g_espkm_ble_mailbox, 0, sizeof(g_espkm_ble_mailbox));

  ESP_LOGI(TAG, "router_task started");

  for (;;) {
    HidReport r;
    bool got_report = xQueueReceive(s_report_q, &r, pdMS_TO_TICKS(100)) == pdTRUE;

    // State machine: both USB and BLE active simultaneously when available.
    bool usb = espkm_usb_is_enumerated();
    bool ble = espkm_ble_is_connected();
    espkm_transport_state_t next = ESPKM_TRANSPORT_NONE;
    if (usb && ble) {
      next = ESPKM_TRANSPORT_BOTH_ACTIVE;
    } else if (usb) {
      next = ESPKM_TRANSPORT_USB_ACTIVE;
    } else if (ble) {
      next = ESPKM_TRANSPORT_BLE_ACTIVE;
    }
    if (g_espkm_transport_state != next) {
      ESP_LOGI(TAG, "transport -> %d (usb_mounted=%d ble_connected=%d)", (int)next, usb ? 1 : 0, ble ? 1 : 0);
    }
    g_espkm_transport_state = next;

    // Distribute into mailboxes; senders gate based on state.
    if (got_report) {
      espkm_mailbox_lock();
      g_espkm_usb_mailbox.latest_report = r;
      g_espkm_usb_mailbox.has_report = true;
      g_espkm_ble_mailbox.latest_report = r;
      g_espkm_ble_mailbox.has_report = true;
      espkm_mailbox_unlock();

#if CONFIG_ESPKM_VERBOSE_LOG
      ESP_LOGI(TAG, "routed report state=%d mods=0x%02x keys=%02x %02x %02x %02x %02x %02x", (int)next, r.modifiers,
               r.keys[0], r.keys[1], r.keys[2], r.keys[3], r.keys[4], r.keys[5]);
#endif

      espkm_dbg_log(ESPKM_DBG_EV_TRANSPORT, (uint16_t)next, r.modifiers, r.keys[0]);
    }
  }
}

void espkm_router_start(QueueHandle_t report_queue) {
  s_report_q = report_queue;
  s_mailbox_mutex = xSemaphoreCreateMutex();
  if (!s_mailbox_mutex) {
    ESP_LOGE(TAG, "Failed to create mailbox mutex");
  }
  xTaskCreate(router_task, "router_task", CONFIG_ESPKM_ROUTER_TASK_STACK, NULL,
              CONFIG_ESPKM_ROUTER_TASK_PRIO, NULL);
}
