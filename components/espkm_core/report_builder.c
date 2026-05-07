#include "espkm/report_builder.h"

#include <string.h>

static void clear_report(HidReport *r) {
  r->modifiers = 0;
  memset(r->keys, 0, sizeof(r->keys));
}

void espkm_report_init(ReportBuilder *rb) {
  clear_report(&rb->cur);
  clear_report(&rb->last_sent);
}

static bool keys_contains(const HidReport *r, uint8_t usage) {
  for (int i = 0; i < 6; i++) {
    if (r->keys[i] == usage) {
      return true;
    }
  }
  return false;
}

static void keys_add(HidReport *r, uint8_t usage) {
  if (usage == 0 || keys_contains(r, usage)) {
    return;
  }
  for (int i = 0; i < 6; i++) {
    if (r->keys[i] == 0) {
      r->keys[i] = usage;
      return;
    }
  }
  // 6KRO overflow: drop (could add error counter later).
}

static void keys_remove(HidReport *r, uint8_t usage) {
  for (int i = 0; i < 6; i++) {
    if (r->keys[i] == usage) {
      r->keys[i] = 0;
    }
  }
}

void espkm_report_apply_key(ReportBuilder *rb, uint8_t hid_usage, bool pressed) {
  if (pressed) {
    keys_add(&rb->cur, hid_usage);
  } else {
    keys_remove(&rb->cur, hid_usage);
  }
}

void espkm_report_apply_mods(ReportBuilder *rb, uint8_t mods, bool pressed) {
  if (pressed) {
    rb->cur.modifiers |= mods;
  } else {
    rb->cur.modifiers &= (uint8_t)~mods;
  }
}

bool espkm_report_snapshot_if_dirty(ReportBuilder *rb, HidReport *out) {
  if (memcmp(&rb->cur, &rb->last_sent, sizeof(HidReport)) == 0) {
    return false;
  }
  rb->last_sent = rb->cur;
  *out = rb->cur;
  return true;
}

