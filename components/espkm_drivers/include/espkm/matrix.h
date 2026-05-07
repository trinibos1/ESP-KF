#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "espkm/core.h"

#ifdef __cplusplus
extern "C" {
#endif

void espkm_matrix_start(QueueHandle_t event_queue);

#ifdef __cplusplus
}
#endif

