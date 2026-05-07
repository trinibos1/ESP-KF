#include "espkm/core.h"

#include "esp_timer.h"

uint32_t kb_millis(void) {
  int64_t us = esp_timer_get_time();
  return (uint32_t)(us / 1000);
}

