// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

void screen_init(void);
void screen_task(void);

/* Screen power for idle and suspend. Stays off if the user turned the screen off */
void screen_set_power(bool on);

/* Shows the battery on the stock UI, 0 while charging and otherwise 1 to 5 */
void screen_set_battery(uint8_t level);

/* Returns true if the keycode was a screen key */
bool screen_process_key(uint16_t keycode, bool pressed);

/* Sends a frame to the screen for the host. The reply goes back on the raw HID interface */
void screen_host_command(const uint8_t *frame, uint8_t len);

/* Chooses custom or stock firmware, saved and applied right away */
bool screen_custom_enabled(void);
void screen_set_custom(bool on);
