#include "keyboard.h"

#include "sdkconfig.h"

uint8_t espkm_keyboard_rows(void) {
  return (uint8_t)CONFIG_ESPKM_MATRIX_ROWS;
}

uint8_t espkm_keyboard_cols(void) {
  return (uint8_t)CONFIG_ESPKM_MATRIX_COLS;
}

