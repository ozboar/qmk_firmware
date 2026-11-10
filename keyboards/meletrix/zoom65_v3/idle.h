// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>

/* Sleeps the CPU once the keyboard is idle, until a key, the knob, an interrupt or the next deadline */
void idle_task(void);

/* Drives the columns and arms the rows and the knob to wake the MCU, also used by stop mode */
void idle_arm_wake_pins(void);
void idle_release_wake_pins(void);

#ifdef IDLE_STATS_ENABLE
/* Fills a raw HID report with the CPU and wait counters since the last report */
void idle_stats_report(uint8_t *data, uint8_t length);
#endif
