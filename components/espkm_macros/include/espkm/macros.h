#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

void espkm_macro_start(QueueHandle_t event_queue);

#ifdef __cplusplus
}
#endif

