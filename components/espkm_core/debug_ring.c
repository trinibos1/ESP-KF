#include "espkm/debug_ring.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "sdkconfig.h"

#if CONFIG_ESPKM_DEBUG
static espkm_dbg_entry_t s_ring[CONFIG_ESPKM_DEBUG_RING_LEN];
static uint32_t s_head = 0;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
#endif

void espkm_dbg_init(void) {
#if CONFIG_ESPKM_DEBUG
  memset(s_ring, 0, sizeof(s_ring));
  s_head = 0;
#endif
}

void espkm_dbg_log(espkm_dbg_event_type_t type, uint16_t a, uint16_t b, uint16_t c) {
#if CONFIG_ESPKM_DEBUG
  uint32_t idx;
  taskENTER_CRITICAL(&s_mux);
  idx = s_head++ % CONFIG_ESPKM_DEBUG_RING_LEN;
  s_ring[idx].t = kb_millis();
  s_ring[idx].type = type;
  s_ring[idx].a = a;
  s_ring[idx].b = b;
  s_ring[idx].c = c;
  taskEXIT_CRITICAL(&s_mux);
#else
  (void)type;
  (void)a;
  (void)b;
  (void)c;
#endif
}

uint32_t espkm_dbg_dump(espkm_dbg_entry_t *out, uint32_t max_entries) {
#if CONFIG_ESPKM_DEBUG
  if (!out || max_entries == 0) {
    return 0;
  }
  taskENTER_CRITICAL(&s_mux);
  uint32_t head = s_head;
  taskEXIT_CRITICAL(&s_mux);

  uint32_t available = head < CONFIG_ESPKM_DEBUG_RING_LEN ? head : CONFIG_ESPKM_DEBUG_RING_LEN;
  uint32_t n = available < max_entries ? available : max_entries;

  // Copy in ring order (oldest -> newest).
  uint32_t start = (head >= n) ? (head - n) : 0;
  for (uint32_t i = 0; i < n; i++) {
    out[i] = s_ring[(start + i) % CONFIG_ESPKM_DEBUG_RING_LEN];
  }
  return n;
#else
  (void)out;
  (void)max_entries;
  return 0;
#endif
}

