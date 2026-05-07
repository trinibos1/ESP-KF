#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t matrix_event_drops;
  uint32_t event_queue_overflows;
  uint32_t report_overwrites;
} espkm_stats_t;

void espkm_stats_init(void);
void espkm_stats_matrix_event_drop(void);
void espkm_stats_event_queue_overflow(void);
void espkm_stats_report_overwrite(void);
espkm_stats_t espkm_stats_snapshot(void);

// Optional periodic logging task (enabled via Kconfig).
void espkm_stats_start_logger(void);

#ifdef __cplusplus
}
#endif

