#include "espkm/matrix.h"

#include <ctype.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdkconfig.h"

#include "espkm/debug_ring.h"
#include "espkm/stats.h"

static const char *TAG = "matrix";

static QueueHandle_t s_event_q;

static int parse_gpio_list(const char *s, int *out, int max_out) {
  int n = 0;
  int cur = -1;
  for (const char *p = s; *p && n < max_out; p++) {
    if (isdigit((unsigned char)*p)) {
      if (cur < 0) cur = 0;
      cur = cur * 10 + (*p - '0');
    } else {
      if (cur >= 0) {
        out[n++] = cur;
        cur = -1;
      }
    }
  }
  if (cur >= 0 && n < max_out) {
    out[n++] = cur;
  }
  return n;
}

static void matrix_task(void *arg) {
  (void)arg;
  const int rows = CONFIG_ESPKM_MATRIX_ROWS;
  const int cols = CONFIG_ESPKM_MATRIX_COLS;
  int row_pins[8] = {0};
  int col_pins[16] = {0};
  int nrows = parse_gpio_list(CONFIG_ESPKM_DIRECT_ROW_PINS, row_pins, 8);
  int ncols = parse_gpio_list(CONFIG_ESPKM_DIRECT_COL_PINS, col_pins, 16);

  if (nrows != rows || ncols != cols) {
    ESP_LOGW(TAG, "Pin list lengths mismatch rows=%d(cols=%d) parsed rows=%d cols=%d", rows, cols, nrows, ncols);
  }

  // Configure columns as inputs with pull-ups.
  for (int c = 0; c < cols && c < ncols; c++) {
    gpio_config_t cfg = {0};
    cfg.pin_bit_mask = 1ULL << col_pins[c];
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&cfg);
  }

  // Configure rows as outputs.
  for (int r = 0; r < rows && r < nrows; r++) {
    gpio_config_t cfg = {0};
    cfg.pin_bit_mask = 1ULL << row_pins[r];
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&cfg);
    gpio_set_level(row_pins[r], 1);
  }

  // Simple debounce: 5ms stable.
  uint8_t stable[8][16] = {0};
  uint8_t last_raw[8][16] = {0};
  uint32_t last_change[8][16] = {0};

  TickType_t last_wake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONFIG_ESPKM_MATRIX_SCAN_PERIOD_MS));
    uint32_t now = kb_millis();

    for (int r = 0; r < rows && r < nrows; r++) {
      // Drive one row low at a time (active low).
      gpio_set_level(row_pins[r], 0);
      esp_rom_delay_us(3);
      for (int c = 0; c < cols && c < ncols; c++) {
        int level = gpio_get_level(col_pins[c]);
        uint8_t pressed = (level == 0) ? 1 : 0;
        if (pressed != last_raw[r][c]) {
          last_raw[r][c] = pressed;
          last_change[r][c] = now;
        }
        uint32_t dt = now - last_change[r][c];
        if (dt >= 5 && stable[r][c] != last_raw[r][c]) {
          stable[r][c] = last_raw[r][c];
          KeyEvent ev = {.row = (uint8_t)r, .col = (uint8_t)c, .pressed = stable[r][c] != 0, .t = now};
          if (xQueueSend(s_event_q, &ev, 0) != pdTRUE) {
            // event loss is dangerous; count via debug ring.
            espkm_dbg_log(ESPKM_DBG_EV_MATRIX, ((uint16_t)r << 8) | (uint16_t)c, 0xEEEE, 0);
            espkm_stats_matrix_event_drop();
            espkm_stats_event_queue_overflow();
          } else {
            espkm_dbg_log(ESPKM_DBG_EV_MATRIX, ((uint16_t)r << 8) | (uint16_t)c, ev.pressed ? 1 : 0, 0);
#if CONFIG_ESPKM_VERBOSE_LOG
            ESP_LOGI(TAG, "edge row=%d col=%d %s row_gpio=%d col_gpio=%d", r, c, ev.pressed ? "DOWN" : "UP",
                     row_pins[r], col_pins[c]);
#endif
          }
        }
      }
      gpio_set_level(row_pins[r], 1);
    }
  }
}

void espkm_matrix_start(QueueHandle_t event_queue) {
  s_event_q = event_queue;
  ESP_LOGI(TAG, "direct matrix start rows=%d cols=%d period=%dms", CONFIG_ESPKM_MATRIX_ROWS, CONFIG_ESPKM_MATRIX_COLS,
           CONFIG_ESPKM_MATRIX_SCAN_PERIOD_MS);
  xTaskCreate(matrix_task, "matrix_task", CONFIG_ESPKM_MATRIX_TASK_STACK / sizeof(StackType_t), NULL,
              CONFIG_ESPKM_MATRIX_TASK_PRIO, NULL);
}
