#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "espkm/core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ESPKM_DBG_EV_MATRIX = 1,
  ESPKM_DBG_EV_CORE_IN = 2,
  ESPKM_DBG_EV_ACTION = 3,
  ESPKM_DBG_EV_REPORT = 4,
  ESPKM_DBG_EV_TRANSPORT = 5,
} espkm_dbg_event_type_t;

typedef struct {
  uint32_t t;
  espkm_dbg_event_type_t type;
  uint16_t a;
  uint16_t b;
  uint16_t c;
} espkm_dbg_entry_t;

void espkm_dbg_init(void);
void espkm_dbg_log(espkm_dbg_event_type_t type, uint16_t a, uint16_t b, uint16_t c);
uint32_t espkm_dbg_dump(espkm_dbg_entry_t *out, uint32_t max_entries);

#ifdef __cplusplus
}
#endif

