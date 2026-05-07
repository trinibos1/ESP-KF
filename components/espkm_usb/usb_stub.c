#include "espkm/usb.h"

// Option 1 default: USB-OTG disabled.
void espkm_usb_start(void) {}
bool espkm_usb_is_enumerated(void) { return false; }

