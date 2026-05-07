#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void espkm_usb_start(void);
bool espkm_usb_is_enumerated(void);

#ifdef __cplusplus
}
#endif
