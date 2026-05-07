#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "espkm/core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ESPKM_TRANSPORT_NONE = 0,
  ESPKM_TRANSPORT_USB_ACTIVE = 1,
  ESPKM_TRANSPORT_BLE_ACTIVE = 2,
  ESPKM_TRANSPORT_BOTH_ACTIVE = 3,
} espkm_transport_state_t;

typedef struct {
  HidReport latest_report;
  bool has_report;
} espkm_mailbox_t;

// Mailboxes are owned by router and read by sender tasks.
extern espkm_mailbox_t g_espkm_usb_mailbox;
extern espkm_mailbox_t g_espkm_ble_mailbox;
extern volatile espkm_transport_state_t g_espkm_transport_state;

void espkm_router_start(QueueHandle_t report_queue);

// Mailbox locking for thread-safe access.
void espkm_mailbox_lock(void);
void espkm_mailbox_unlock(void);

#ifdef __cplusplus
}
#endif

