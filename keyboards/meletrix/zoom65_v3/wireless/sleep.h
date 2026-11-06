// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>

/* Turns the lights off when idle in wireless mode, then stops the MCU until a key wakes it */
void sleep_task(void);

/* The idle stages have the LEDs off */
bool sleep_leds_off(void);
