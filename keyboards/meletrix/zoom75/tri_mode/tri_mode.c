// Copyright 2022 QMK
// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "tri_mode.h"
#include "gpio.h"
#define LCD_WAKE_PIN A10

// Define custom_keycodes.
// enum custom_keycodes {
//   LCD_TG = SAFE_RANGE,
//   LCD_MD,
//   LCD_MI
// };

// Setup function, happens before most anything is started. Good for hardware setup that you want running very early.
void keyboard_pre_init_kb(void) {
  gpio_set_pin_output_push_pull(LCD_WAKE_PIN);
  gpio_write_pin_high(LCD_WAKE_PIN);
}

// Setup function, happens at the end of the firmware's startup process.
void keyboard_post_init_kb(void) {
  // debug_enable   = true;
  // debug_matrix   = true;
  // debug_keyboard = true;
  // debug_mouse    = true;
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
  switch (keycode) {
    case LCD_TG:
      if (record->event.pressed) {
        gpio_toggle_pin(LCD_WAKE_PIN);
      }
      return false;
    default:
      return process_record_user(keycode, record);
  }
}
