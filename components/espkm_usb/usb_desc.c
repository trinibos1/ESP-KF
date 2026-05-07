#include <stdint.h>

#include "tusb.h"

// HID report descriptor — keyboard only.
const uint8_t espkm_hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD)),
};

// HID-only USB configuration descriptor
enum {
  ITF_NUM_HID = 0,
  ITF_NUM_TOTAL
};

enum {
  EPNUM_HID_IN = 0x81,
};

#define ESPKM_USB_CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t s_cfg_desc_fs[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, ESPKM_USB_CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, false, sizeof(espkm_hid_report_descriptor),
                       EPNUM_HID_IN, 16, 10),
};

const uint8_t *espkm_usb_configuration_descriptor_fs(void) {
  return s_cfg_desc_fs;
}