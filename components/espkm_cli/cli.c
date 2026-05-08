#include "espkm/cli.h"

#include <inttypes.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "linenoise/linenoise.h"

#include "sdkconfig.h"

#include "espkm/ble.h"
#include "espkm/debug_ring.h"
#include "espkm/router.h"
#include "espkm/stats.h"

static const char *TAG = "espkm_cli";

static struct {
  struct arg_int *n;
  struct arg_end *end;
} s_ring_args;

static int cmd_stats(int argc, char **argv) {
  (void)argc;
  (void)argv;
  espkm_stats_t s = espkm_stats_snapshot();
  printf("stats: matrix_drops=%" PRIu32 " eventq_overflows=%" PRIu32 " report_overwrites=%" PRIu32 "\n", s.matrix_event_drops,
         s.event_queue_overflows, s.report_overwrites);
  return 0;
}

static int cmd_transport(int argc, char **argv) {
  (void)argc;
  (void)argv;
  printf("transport: %d (0=NONE 1=USB 2=BLE 3=BOTH)\n", (int)g_espkm_transport_state);
  return 0;
}

static int cmd_ring(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&s_ring_args);
  if (nerrors) {
    arg_print_errors(stderr, s_ring_args.end, argv[0]);
    return 1;
  }
  uint32_t n = 4;
  if (s_ring_args.n->count) {
    n = (uint32_t)s_ring_args.n->ival[0];
  }
  if (n > 8) n = 8;

  espkm_dbg_entry_t entries[8];
  uint32_t got = espkm_dbg_dump(entries, n);
  printf("ring: %u entries (max 8)\n", (unsigned)got);
  for (uint32_t i = 0; i < got; i++) {
    const espkm_dbg_entry_t *e = &entries[i];
    printf("%u: t=%" PRIu32 " type=%u a=%u b=%u c=%u\n", (unsigned)i, e->t, (unsigned)e->type, (unsigned)e->a,
           (unsigned)e->b, (unsigned)e->c);
  }
  return 0;
}

static int cmd_pins(int argc, char **argv) {
  (void)argc;
  (void)argv;
  printf("matrix: rows=%d cols=%d row_gpios=%s col_gpios=%s\n", CONFIG_ESPKM_MATRIX_ROWS, CONFIG_ESPKM_MATRIX_COLS,
         CONFIG_ESPKM_DIRECT_ROW_PINS, CONFIG_ESPKM_DIRECT_COL_PINS);
  return 0;
}

static struct {
  struct arg_str *mode;
  struct arg_end *end;
} s_ble_args;

static int cmd_ble(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&s_ble_args);
  if (nerrors) {
    arg_print_errors(stderr, s_ble_args.end, argv[0]);
    return 1;
  }
  const char *mode = (s_ble_args.mode->count) ? s_ble_args.mode->sval[0] : "status";

  if (strcmp(mode, "status") == 0) {
    printf("ble: connected=%d advertising=%d\n", espkm_ble_is_connected() ? 1 : 0, espkm_ble_is_advertising() ? 1 : 0);
    return 0;
  }
  if (strcmp(mode, "on") == 0) {
    espkm_ble_set_enabled(true);
    printf("ble: enabled\n");
    return 0;
  }
  if (strcmp(mode, "off") == 0) {
    espkm_ble_set_enabled(false);
    printf("ble: disabled\n");
    return 0;
  }
  if (strcmp(mode, "clear") == 0) {
    bool ok = espkm_ble_clear_bonds();
    printf("ble: clear bonds %s\n", ok ? "ok" : "failed");
    return ok ? 0 : 1;
  }
  printf("usage: ble [status|on|off|clear]\n");
  return 1;
}

static void register_one(const esp_console_cmd_t *cmd) {
  esp_err_t err = esp_console_cmd_register(cmd);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "command '%s' disabled: %s", cmd->command, esp_err_to_name(err));
  }
}

static void register_commands(void) {
  esp_console_cmd_t cmd = {
      .command = "stats",
      .help = "Show counters snapshot",
      .hint = NULL,
      .func = &cmd_stats,
      .argtable = NULL,
  };
  register_one(&cmd);

  cmd = (esp_console_cmd_t){
      .command = "transport",
      .help = "Show router transport state",
      .hint = NULL,
      .func = &cmd_transport,
      .argtable = NULL,
  };
  register_one(&cmd);

  s_ring_args.n = arg_int0(NULL, NULL, "<N>", "entries (default 32)");
  s_ring_args.end = arg_end(1);
  cmd = (esp_console_cmd_t){
      .command = "ring",
      .help = "Dump last N debug ring entries (default 4, max 8)",
      .hint = NULL,
      .func = &cmd_ring,
      .argtable = &s_ring_args,
  };
  register_one(&cmd);

  cmd = (esp_console_cmd_t){
      .command = "pins",
      .help = "Show configured matrix GPIOs",
      .hint = NULL,
      .func = &cmd_pins,
      .argtable = NULL,
  };
  register_one(&cmd);

  s_ble_args.mode = arg_str0(NULL, NULL, "<status|on|off|clear>", "BLE control/status");
  s_ble_args.end = arg_end(1);
  cmd = (esp_console_cmd_t){
      .command = "ble",
      .help = "BLE status/control",
      .hint = NULL,
      .func = &cmd_ble,
      .argtable = &s_ble_args,
  };
  register_one(&cmd);
}

void espkm_cli_start(void) {
#if !CONFIG_ESPKM_CLI_ENABLE
  ESP_LOGI(TAG, "CLI disabled at compile time (CONFIG_ESPKM_CLI_ENABLE=n)");
  return;
#else
  esp_console_repl_t *repl = NULL;

  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  repl_config.prompt = "espkm> ";

   // Use the active stdio driver (UART0 by default, since USB Serial/JTAG is disabled
   // to allow TinyUSB HID to own the USB PHY). This REPL appears on that port.
  esp_err_t err = esp_console_new_repl_stdio(&repl_config, &repl);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "CLI disabled: esp_console_new_repl_stdio failed: %s", esp_err_to_name(err));
    return;
  }

  linenoiseSetDumbMode(1);
  linenoiseHistorySetMaxLen(50);

  register_commands();

  err = esp_console_start_repl(repl);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "CLI disabled: esp_console_start_repl failed: %s", esp_err_to_name(err));
    return;
  }

  ESP_LOGI(TAG, "CLI ready on stdio");
#endif
}
