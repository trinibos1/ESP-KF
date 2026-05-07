#include "espkm/usb.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "sdkconfig.h"

#include "tinyusb_default_config.h"

#include "espkm/debug_ring.h"
#include "espkm/router.h"
#include "espkm/stats.h"

static const char *TAG = "usb";
static bool s_started = false;

bool espkm_usb_is_enumerated(void) {
#if CONFIG_ESPKM_USB_OTG_ENABLE
  return tud_mounted();
#else
  return false;
#endif
}

static void usb_task(void *arg) {
  (void)arg;
  ESP_LOGI(TAG, "usb_task started");

  TickType_t last_wake = xTaskGetTickCount();
  uint32_t loop_count = 0;
  uint32_t skip_count = 0;
  bool prev_mounted = false;
  bool prev_hid_ready = false;

  for (;;) {
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONFIG_ESPKM_USB_POLL_MS));

    loop_count++;
    espkm_transport_state_t state = g_espkm_transport_state;
    bool should_send = (state == ESPKM_TRANSPORT_USB_ACTIVE || state == ESPKM_TRANSPORT_BOTH_ACTIVE);

    if (!should_send) {
      if ((loop_count % 1000) == 0) {
        ESP_LOGD(TAG, "usb_task: skipped (wrong state=%d)", (int)state);
      }
      continue;
    }

    // Check and log USB state changes
    bool mounted = tud_mounted();
    if (mounted != prev_mounted) {
      ESP_LOGI(TAG, "USB mounted state: %s", mounted ? "true" : "false");
      if (mounted) {
        ESP_LOGI(TAG, "USB device enumerated successfully!");
      }
      prev_mounted = mounted;
    }
    if (!mounted) {
      ESP_LOGD(TAG, "usb_task: not mounted");
      continue;
    }

    bool hid_ready = tud_hid_ready();
    if (hid_ready != prev_hid_ready) {
      ESP_LOGI(TAG, "HID ready state: %s", hid_ready ? "true" : "false");
      prev_hid_ready = hid_ready;
    }
    if (!hid_ready) {
      ESP_LOGD(TAG, "usb_task: hid not ready");
      continue;
    }

    // Consume report from mailbox atomically
    espkm_mailbox_lock();
    bool has_report = g_espkm_usb_mailbox.has_report;
    HidReport r = g_espkm_usb_mailbox.latest_report;
    g_espkm_usb_mailbox.has_report = false;  // consume
    espkm_mailbox_unlock();

    if (!has_report) {
      skip_count++;
      if ((skip_count % 100) == 0) {
        ESP_LOGD(TAG, "usb_task: no report in mailbox");
      }
      continue;
    }

    ESP_LOGI(TAG, "usb_task: sending report mods=0x%02x keys=%02x %02x %02x %02x %02x %02x",
             r.modifiers, r.keys[0], r.keys[1], r.keys[2], r.keys[3], r.keys[4], r.keys[5]);
    bool ok = tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, r.modifiers, r.keys);
    if (!ok) {
      ESP_LOGW(TAG, "HID drop (EP full or not ready)");
      // Re-queue the report so it isn't lost
      espkm_mailbox_lock();
      if (!g_espkm_usb_mailbox.has_report) {
        g_espkm_usb_mailbox.latest_report = r;
        g_espkm_usb_mailbox.has_report = true;
      }
      espkm_mailbox_unlock();
    }
  }
}

// ---------------------------------------------------------------------------
// CDC console (only compiled when TinyUSB CDC is enabled)
// ---------------------------------------------------------------------------
#if CFG_TUD_CDC

static void cdc_write_str(const char *s) {
  if (!s) return;
  tud_cdc_write_str(s);
  tud_cdc_write_flush();
}

static void cdc_writef(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  cdc_write_str(buf);
}

static void cdc_cmd_help(void) {
  cdc_write_str(
      "espkm cdc console commands:\r\n"
      "  help            - this help\r\n"
      "  stats           - counters snapshot\r\n"
      "  transport       - current transport state\r\n"
      "  ring [N]        - dump last N debug entries (default 32)\r\n");
}

static void cdc_cmd_stats(void) {
  espkm_stats_t s = espkm_stats_snapshot();
  cdc_writef("stats: matrix_drops=%" PRIu32 " eventq_overflows=%" PRIu32
             " report_overwrites=%" PRIu32 "\r\n",
             s.matrix_event_drops, s.event_queue_overflows, s.report_overwrites);
}

static void cdc_cmd_transport(void) {
  cdc_writef("transport: %d (0=NONE 1=USB 2=BLE 3=BOTH)\r\n",
             (int)g_espkm_transport_state);
}

static void cdc_cmd_ring(uint32_t n) {
  if (n == 0) n = 32;
  if (n > 256) n = 256;
  espkm_dbg_entry_t entries[256];
  uint32_t got = espkm_dbg_dump(entries, n);
  cdc_writef("ring: %u entries\r\n", (unsigned)got);
  for (uint32_t i = 0; i < got; i++) {
    const espkm_dbg_entry_t *e = &entries[i];
    cdc_writef("%" PRIu32 " type=%u a=%u b=%u c=%u\r\n", e->t,
               (unsigned)e->type, (unsigned)e->a, (unsigned)e->b,
               (unsigned)e->c);
  }
}

static uint32_t parse_u32(const char *s) {
  uint32_t v = 0;
  while (*s && isspace((unsigned char)*s)) s++;
  while (*s && isdigit((unsigned char)*s))
    v = v * 10u + (uint32_t)(*s - '0'), s++;
  return v;
}

static void cdc_handle_line(char *line) {
  while (*line && isspace((unsigned char)*line)) line++;
  size_t len = strlen(line);
  while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' ||
                     isspace((unsigned char)line[len - 1])))
    line[--len] = 0;
  if (len == 0) return;

  if (strcmp(line, "help") == 0) { cdc_cmd_help(); return; }
  if (strcmp(line, "stats") == 0) { cdc_cmd_stats(); return; }
  if (strcmp(line, "transport") == 0) { cdc_cmd_transport(); return; }
  if (strncmp(line, "ring", 4) == 0) { cdc_cmd_ring(parse_u32(line + 4)); return; }

  cdc_writef("unknown cmd: %s\r\n", line);
  cdc_cmd_help();
}

static void cdc_task(void *arg) {
  (void)arg;
  ESP_LOGI(TAG, "cdc_task started");
  char line[128];
  size_t used = 0;

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!tud_cdc_available()) continue;
    while (tud_cdc_available()) {
      uint8_t ch;
      if (tud_cdc_read(&ch, 1) != 1) break;
      if (ch == '\r' || ch == '\n') {
        line[used] = 0;
        if (used > 0) cdc_handle_line(line);
        used = 0;
        continue;
      }
      if (used + 1 < sizeof(line))
        line[used++] = (char)ch;
      else
        used = 0, cdc_write_str("error: line too long\r\n");
    }
  }
}

#endif // CFG_TUD_CDC

// ---------------------------------------------------------------------------
// TinyUSB HID callbacks (always required)
// ---------------------------------------------------------------------------

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  (void)instance;
  extern const uint8_t espkm_hid_report_descriptor[];
  return espkm_hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
  (void)instance; (void)report_id; (void)report_type;
  (void)buffer;   (void)reqlen;
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
  (void)instance; (void)report_id; (void)report_type;
  (void)buffer;   (void)bufsize;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void espkm_usb_start(void) {
#if CONFIG_ESPKM_USB_OTG_ENABLE
  ESP_LOGI(TAG, "espkm_usb_start called");
  if (s_started) {
    ESP_LOGI(TAG, "USB already started");
    return;
  }
  s_started = true;

  extern const uint8_t *espkm_usb_configuration_descriptor_fs(void);
  const uint8_t *fs_desc = espkm_usb_configuration_descriptor_fs();
  ESP_LOGI(TAG, "fs_desc ptr = %p", (void *)fs_desc);
  if (!fs_desc) {
    ESP_LOGE(TAG, "NULL FS descriptor — aborting");
    s_started = false;
    return;
  }

  tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
  tusb_cfg.descriptor.full_speed_config = fs_desc;
#if (TUD_OPT_HIGH_SPEED)
  tusb_cfg.descriptor.high_speed_config = fs_desc;
#endif

  ESP_LOGI(TAG, "installing TinyUSB driver");
  esp_err_t err = tinyusb_driver_install(&tusb_cfg);
  vTaskDelay(pdMS_TO_TICKS(100)); // let UART flush before potential error log
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "tinyusb_driver_install FAILED: 0x%x %s",
             err, esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(200));
    s_started = false;
    return;
  }

  ESP_LOGI(TAG, "TinyUSB installed successfully");

#ifndef CONFIG_TINYUSB_TASK_ENABLE
  ESP_LOGW(TAG, "IDF TinyUSB task disabled - usb_task will manually call tud_task()");
#else
  ESP_LOGI(TAG, "IDF TinyUSB task enabled (auto-runs tud_task)");
#endif

  xTaskCreate(usb_task, "usb_task", CONFIG_ESPKM_USB_TASK_STACK, NULL,
              CONFIG_ESPKM_USB_TASK_PRIO, NULL);
  ESP_LOGI(TAG, "USB HID send task created");

#if CFG_TUD_CDC
  xTaskCreate(cdc_task, "cdc_task", 2048, NULL, 5, NULL);
  ESP_LOGI(TAG, "USB CDC task created");
#endif

#else
  ESP_LOGI(TAG, "USB not enabled (CONFIG_ESPKM_USB_OTG_ENABLE not set)");
#endif // CONFIG_ESPKM_USB_OTG_ENABLE
}