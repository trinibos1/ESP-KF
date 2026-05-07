#pragma once

#include <stdbool.h>

#include "espkm/core.h"

#ifdef __cplusplus
extern "C" {
#endif

void espkm_ble_start(void);
bool espkm_ble_is_connected(void);
bool espkm_ble_is_advertising(void);
void espkm_ble_set_enabled(bool enabled);
bool espkm_ble_send_report(const HidReport *report);
bool espkm_ble_clear_bonds(void);

#ifdef __cplusplus
}
#endif
