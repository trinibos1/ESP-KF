#include "espkm/ble.h"

#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "nvs_flash.h"

#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "host/ble_store.h"
#include "os/os_mbuf.h"
#include "store/config/ble_store_config.h"

#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/bas/ble_svc_bas.h"
#include "services/dis/ble_svc_dis.h"

#include "espkm/router.h"

static const char *TAG = "espkm_ble";

void ble_store_config_init(void);

static volatile bool s_enabled = true;
static volatile bool s_connected = false;
static volatile bool s_advertising = false;
static volatile bool s_input_subscribed = false;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_own_addr_type = BLE_OWN_ADDR_RANDOM;
static uint16_t s_input_report_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_boot_input_report_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_protocol_mode = 1; // 1=report protocol, 0=boot protocol.
static HidReport s_last_report;

static void adv_start(void);

static const uint8_t s_hid_info[] = {
    0x11, 0x01, // HID version 1.11
    0x00,       // country code
    0x02,       // normally connectable
};

static const uint8_t s_hid_report_ref[] = {
    0x00, // report id (0 = no report ID prefix in the report payload)
    0x01, // input report
};

static const uint8_t s_hid_report_map[] = {
    0x05, 0x01, // Usage Page (Generic Desktop)
    0x09, 0x06, // Usage (Keyboard)
    0xA1, 0x01, // Collection (Application)
    0x05, 0x07, //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0, //   Usage Minimum (Keyboard LeftControl)
    0x29, 0xE7, //   Usage Maximum (Keyboard Right GUI)
    0x15, 0x00, //   Logical Minimum (0)
    0x25, 0x01, //   Logical Maximum (1)
    0x75, 0x01, //   Report Size (1)
    0x95, 0x08, //   Report Count (8)
    0x81, 0x02, //   Input (Data,Var,Abs)
    0x95, 0x01, //   Report Count (1)
    0x75, 0x08, //   Report Size (8)
    0x81, 0x01, //   Input (Const,Array,Abs)
    0x95, 0x06, //   Report Count (6)
    0x75, 0x08, //   Report Size (8)
    0x15, 0x00, //   Logical Minimum (0)
    0x25, 0x65, //   Logical Maximum (101)
    0x05, 0x07, //   Usage Page (Keyboard/Keypad)
    0x19, 0x00, //   Usage Minimum (Reserved)
    0x29, 0x65, //   Usage Maximum (Keyboard Application)
    0x81, 0x00, //   Input (Data,Array,Abs)
    0xC0,       // End Collection
};

static int append_flat(struct os_mbuf *om, const void *data, uint16_t len) {
  return os_mbuf_append(om, data, len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static void fill_ble_keyboard_report(uint8_t out[8], const HidReport *report) {
  out[0] = report->modifiers;
  out[1] = 0;
  memcpy(&out[2], report->keys, 6);
}

static int hid_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  const uintptr_t id = (uintptr_t)arg;

  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    if (id == 5) {
      return 0; // HID control point.
    }
    if (id == 6) {
      uint8_t mode = 1;
      if (OS_MBUF_PKTLEN(ctxt->om) > 0 && ble_hs_mbuf_to_flat(ctxt->om, &mode, sizeof(mode), NULL) == 0) {
        s_protocol_mode = mode ? 1 : 0;
      }
      return 0;
    }
    if (id == 9 || id == 10) {
      return 0; // CCCD writes handled by Nimble stack subscription events.
    }
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }

  if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR && ctxt->op != BLE_GATT_ACCESS_OP_READ_DSC) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  switch (id) {
  case 1:
    return append_flat(ctxt->om, s_hid_info, sizeof(s_hid_info));
  case 2:
    return append_flat(ctxt->om, s_hid_report_map, sizeof(s_hid_report_map));
  case 3:
  case 4: {
    uint8_t report[8];
    fill_ble_keyboard_report(report, &s_last_report);
    return append_flat(ctxt->om, report, sizeof(report));
  }
  case 6:
    return append_flat(ctxt->om, &s_protocol_mode, sizeof(s_protocol_mode));
  case 7:
  case 8:
    return append_flat(ctxt->om, s_hid_report_ref, sizeof(s_hid_report_ref));
  case 9:
  case 10: {
    uint16_t cccd_val = 0x0001; // Notifications enabled by default
    return append_flat(ctxt->om, &cccd_val, sizeof(cccd_val));
  }
  default:
    return BLE_ATT_ERR_ATTR_NOT_FOUND;
  }
}


static const ble_uuid16_t s_uuid_hid_service = BLE_UUID16_INIT(0x1812);

static const struct ble_gatt_dsc_def s_input_report_dscs[] = {
    {
        .uuid = BLE_UUID16_DECLARE(0x2908), // Report Reference
        .att_flags = BLE_ATT_F_READ,
        .access_cb = hid_access,
        .arg = (void *)(uintptr_t)7,
    },
    {0},
};

static const struct ble_gatt_dsc_def s_boot_input_report_dscs[] = {
    {
        .uuid = BLE_UUID16_DECLARE(0x2908), // Report Reference
        .att_flags = BLE_ATT_F_READ,
        .access_cb = hid_access,
        .arg = (void *)(uintptr_t)8,
    },
    {0},
};

static const struct ble_gatt_chr_def s_hid_chrs[] = {
    {
        // HID Information
        .uuid = BLE_UUID16_DECLARE(0x2A4A),
        .access_cb = hid_access,
        .arg = (void *)(uintptr_t)1,
        .flags = BLE_GATT_CHR_F_READ,
    },
    {
        // Report Map
        .uuid = BLE_UUID16_DECLARE(0x2A4B),
        .access_cb = hid_access,
        .arg = (void *)(uintptr_t)2,
        .flags = BLE_GATT_CHR_F_READ,
    },
    {
        // Boot Keyboard Input Report
        .uuid = BLE_UUID16_DECLARE(0x2A22),
        .access_cb = hid_access,
        .arg = (void *)(uintptr_t)3,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_boot_input_report_handle,
        .descriptors = (struct ble_gatt_dsc_def *)s_boot_input_report_dscs,
    },
    {
        // Input Report
        .uuid = BLE_UUID16_DECLARE(0x2A4D),
        .access_cb = hid_access,
        .arg = (void *)(uintptr_t)4,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_input_report_handle,
        .descriptors = (struct ble_gatt_dsc_def *)s_input_report_dscs,
    },
    {
        // HID Control Point
        .uuid = BLE_UUID16_DECLARE(0x2A4C),
        .access_cb = hid_access,
        .arg = (void *)(uintptr_t)5,
        .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        // Protocol Mode
        .uuid = BLE_UUID16_DECLARE(0x2A4E),
        .access_cb = hid_access,
        .arg = (void *)(uintptr_t)6,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {0},
};

static const struct ble_gatt_svc_def s_hid_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812), // HID service
        .characteristics = s_hid_chrs,
    },
    {0},
};

bool espkm_ble_is_connected(void) {
  return s_connected;
}

bool espkm_ble_is_advertising(void) {
  return s_advertising;
}

void espkm_ble_set_enabled(bool enabled) {
  s_enabled = enabled;
  // We keep the stack running; this only gates advertising/connectability.
  if (!enabled) {
    (void)ble_gap_adv_stop();
    s_advertising = false;
    s_connected = false;
  } else {
    adv_start();
  }
}

bool espkm_ble_clear_bonds(void) {
#if CONFIG_ESPKM_BLE_ENABLE
  int rc = ble_store_clear();
  if (rc != 0) {
    ESP_LOGW(TAG, "ble_store_clear rc=%d", rc);
    return false;
  }
  ESP_LOGI(TAG, "BLE bond store cleared");
  return true;
#else
  return false;
#endif
}

static int gap_event(struct ble_gap_event *event, void *arg);

static void adv_start(void) {
  if (!s_enabled) {
    return;
  }

  struct ble_gap_adv_params adv_params;
  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  struct ble_hs_adv_fields fields;
  memset(&fields, 0, sizeof(fields));

  const char *name = ble_svc_gap_device_name();
  fields.name = (const uint8_t *)name;
  fields.name_len = (uint8_t)strlen(name);
  fields.name_is_complete = 1;

  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.uuids16 = &s_uuid_hid_service;
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;
  fields.appearance = 0x03C1; // HID keyboard.
  fields.appearance_is_present = 1;

  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "ble_gap_adv_set_fields rc=%d", rc);
    return;
  }

  rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, gap_event, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "ble_gap_adv_start rc=%d", rc);
    return;
  }

  s_advertising = true;
  ESP_LOGI(TAG, "advertising started (name=%s)", name);
}

static int gap_event(struct ble_gap_event *event, void *arg) {
  (void)arg;
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
      s_connected = true;
      s_advertising = false;
      s_conn_handle = event->connect.conn_handle;
      ESP_LOGI(TAG, "connected");
    } else {
      s_connected = false;
      s_advertising = false;
      ESP_LOGW(TAG, "connect failed; status=%d", event->connect.status);
      adv_start();
    }
    return 0;

  case BLE_GAP_EVENT_DISCONNECT:
    s_connected = false;
    s_advertising = false;
    s_input_subscribed = false;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    ESP_LOGI(TAG, "disconnected; reason=%d", event->disconnect.reason);
    adv_start();
    return 0;

  case BLE_GAP_EVENT_SUBSCRIBE:
    if (event->subscribe.attr_handle == s_input_report_handle || event->subscribe.attr_handle == s_boot_input_report_handle) {
      s_input_subscribed = event->subscribe.cur_notify != 0;
      ESP_LOGI(TAG, "input notify %s", s_input_subscribed ? "enabled" : "disabled");
    }
    return 0;

  case BLE_GAP_EVENT_ENC_CHANGE:
    ESP_LOGI(TAG, "encryption change status=%d handle=%d", event->enc_change.status, event->enc_change.conn_handle);
    if (event->enc_change.status != 0) {
      ESP_LOGW(TAG, "encryption failed (status=%d). deleting local bond records...", event->enc_change.status);
      struct ble_gap_conn_desc desc_err;
      if (ble_gap_conn_find(event->enc_change.conn_handle, &desc_err) == 0) {
        (void)ble_store_util_delete_peer(&desc_err.peer_id_addr);
      }
      // We don't disconnect here anymore. Let the host decide what to do.
      return 0;
    }
    struct ble_gap_conn_desc desc_enc;
    if (ble_gap_conn_find(event->enc_change.conn_handle, &desc_enc) == 0) {
      ESP_LOGI(TAG, "encryption: encrypted=%d authenticated=%d bonded=%d", desc_enc.sec_state.encrypted,
               desc_enc.sec_state.authenticated, desc_enc.sec_state.bonded);
    }
    return 0;

  case BLE_GAP_EVENT_REPEAT_PAIRING:
    ESP_LOGW(TAG, "repeat pairing; deleting stale peer bond");
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
      (void)ble_store_util_delete_peer(&desc.peer_id_addr);
    }
    return BLE_GAP_REPEAT_PAIRING_RETRY;

  case BLE_GAP_EVENT_ADV_COMPLETE:
    s_advertising = false;
    ESP_LOGI(TAG, "adv complete; reason=%d", event->adv_complete.reason);
    adv_start();
    return 0;

  default:
    return 0;
  }
}

static void on_stack_reset(int reason) {
  ESP_LOGW(TAG, "nimble reset; reason=%d", reason);
}

static void on_stack_sync(void) {
  // Fixed static-random dev identity while the HID service is changing.
  // This helps hosts stop reusing stale bonds for the public MAC.
  // NimBLE stores addresses little-endian: byte[5] is MSB.
  // Static random addresses require the top two bits of the MSB = 0b11 (>= 0xC0).
  uint8_t dev_addr[6] = {0x32, 0x20, 0x05, 0x03, 0x02, 0xC1};
  int rc = ble_hs_id_set_rnd(dev_addr);
  if (rc != 0) {
    ESP_LOGW(TAG, "ble_hs_id_set_rnd rc=%d", rc);
  }

  rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
  if (rc != 0) {
    ESP_LOGE(TAG, "ble_hs_id_infer_auto rc=%d", rc);
    return;
  }
  ESP_LOGI(TAG, "own_addr_type=%u", (unsigned)s_own_addr_type);
  adv_start();
}

static void host_task(void *param) {
  (void)param;
  ESP_LOGI(TAG, "nimble host task started");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

bool espkm_ble_send_report(const HidReport *report) {
#if CONFIG_ESPKM_BLE_ENABLE
  if (!s_connected || !s_input_subscribed || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    return false;
  }

  uint8_t data[8];
  fill_ble_keyboard_report(data, report);
  struct os_mbuf *om = ble_hs_mbuf_from_flat(data, sizeof(data));
  if (om == NULL) {
    return false;
  }
  int rc = ble_gatts_notify_custom(s_conn_handle, s_input_report_handle, om);
  if (rc != 0) {
    ESP_LOGW(TAG, "notify report rc=%d", rc);
    return false;
  }

  om = ble_hs_mbuf_from_flat(data, sizeof(data));
  if (om != NULL) {
    int rc2 = ble_gatts_notify_custom(s_conn_handle, s_boot_input_report_handle, om);
    if (rc2 != 0) {
      os_mbuf_free(om);
    }
  }
  s_last_report = *report;
  return true;
#else
  (void)report;
  return false;
#endif
}

static void ble_report_task(void *param) {
  (void)param;
  HidReport last = {0};
  uint32_t blocked_logs = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(8));
    espkm_transport_state_t state = g_espkm_transport_state;
    bool should_send = (state == ESPKM_TRANSPORT_BLE_ACTIVE || state == ESPKM_TRANSPORT_BOTH_ACTIVE);
    if (!should_send || !g_espkm_ble_mailbox.has_report) {
      continue;
    }
    HidReport report = g_espkm_ble_mailbox.latest_report;
    if (memcmp(&report, &last, sizeof(report)) == 0) {
      continue;
    }
    if (espkm_ble_send_report(&report)) {
      ESP_LOGI(TAG, "sent report mods=0x%02x keys=%02x %02x %02x %02x %02x %02x", report.modifiers, report.keys[0],
               report.keys[1], report.keys[2], report.keys[3], report.keys[4], report.keys[5]);
      last = report;
    } else if (blocked_logs < 8) {
      blocked_logs++;
      ESP_LOGW(TAG, "report not sent connected=%d subscribed=%d state=%d", s_connected ? 1 : 0, s_input_subscribed ? 1 : 0,
               (int)g_espkm_transport_state);
    }
  }
}

void espkm_ble_start(void) {
#if CONFIG_ESPKM_BLE_ENABLE
  esp_err_t ret = nimble_port_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "BLE disabled: nimble_port_init failed: %s", esp_err_to_name(ret));
    return;
  }

  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_svc_bas_init();
  ble_svc_dis_init();

  ble_svc_gap_device_name_set("espkm-hid2");
  ble_svc_dis_manufacturer_name_set("Espressif");
  ble_svc_dis_model_number_set("espkm-v2");

  int rc = ble_gatts_count_cfg(s_hid_svcs);
  if (rc != 0) {
    ESP_LOGE(TAG, "BLE HID disabled: count cfg rc=%d", rc);
    return;
  }
    rc = ble_gatts_add_svcs(s_hid_svcs);
    if (rc != 0) {
      ESP_LOGE(TAG, "BLE HID disabled: add svcs rc=%d", rc);
      return;
    }

   // GATT will be started automatically by the host stack when it starts.
   // Explicit call to ble_gatts_start() is not needed here and can cause issues.

   ble_hs_cfg.reset_cb = on_stack_reset;
  ble_hs_cfg.sync_cb = on_stack_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  // Bring-up mode: HID characteristics are intentionally readable/notifiable
  // without encryption. Keep the store initialized for CCCD/local records, but
  // do not request bonding yet; stale host bonds were causing connect/disconnect
  // loops while the HID service shape is still changing.
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_store_config_init();
  ESP_LOGI(TAG, "BLE security: bonding enabled");
  // Debug: log service and characteristic UUID pointers before starting GATT
  ESP_LOGI(TAG, "HID service uuid ptr=%p", s_hid_svcs[0].uuid);
  for (int i = 0; s_hid_chrs[i].uuid != NULL; i++) {
    ESP_LOGI(TAG, "chr %d uuid=%p val_handle=%p access_cb=%p", i,
             s_hid_chrs[i].uuid, s_hid_chrs[i].val_handle, s_hid_chrs[i].access_cb);
  }

  nimble_port_freertos_init(host_task);
  xTaskCreate(ble_report_task, "ble_report_task", 4096 / sizeof(StackType_t), NULL, 6, NULL);
#endif
}
