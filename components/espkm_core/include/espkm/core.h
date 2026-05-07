#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t keycode_t;

typedef struct {
  uint8_t row;
  uint8_t col;
  bool pressed;
  uint32_t t; // ms
} KeyEvent;

typedef struct {
  uint8_t modifiers;
  uint8_t keys[6];
} HidReport;

// Common callback signature used by feature stages to emit "atomic actions"
// as keycode press/release events into the downstream pipeline.
typedef void (*espkm_emit_action_fn)(void *ctx, keycode_t kc, bool pressed);

uint32_t kb_millis(void);

// Starts the core pipeline task: eventQueue -> pipeline -> reportQueue.
// Queues are owned/created by the app (main).
void espkm_core_start(void *event_queue, void *report_queue);

#ifdef __cplusplus
}
#endif
