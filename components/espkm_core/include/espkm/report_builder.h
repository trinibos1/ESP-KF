#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "espkm/core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  HidReport cur;
  HidReport last_sent;
} ReportBuilder;

void espkm_report_init(ReportBuilder *rb);
void espkm_report_apply_key(ReportBuilder *rb, uint8_t hid_usage, bool pressed);
void espkm_report_apply_mods(ReportBuilder *rb, uint8_t mods, bool pressed);
bool espkm_report_snapshot_if_dirty(ReportBuilder *rb, HidReport *out);

#ifdef __cplusplus
}
#endif

