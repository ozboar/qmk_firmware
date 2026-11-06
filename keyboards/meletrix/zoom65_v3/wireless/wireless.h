// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WL_DEV_USB = 0,
    WL_DEV_BT1 = 1,
    WL_DEV_BT2 = 2,
    WL_DEV_BT3 = 3,
    WL_DEV_BT4 = 4,
    WL_DEV_BT5 = 5,
    WL_DEV_2G4 = 6,
} wl_dev_t;

/* Selects the device on press. Holding a wireless device's key starts pairing */
void wireless_device_key(wl_dev_t device, bool pressed);

/* Moves back to the last used wireless device */
void wireless_resume(void);

/* Shows the battery level on the number row while held */
void wireless_battery_key(bool pressed);

wl_dev_t wireless_device(void);
uint8_t  wireless_battery(void);

/* True after pairing or reconnecting timed out or the battery ran out, cleared by wireless_on_wake */
bool wireless_should_sleep(void);
void wireless_on_wake(void);

/* True while reports are still on their way to the module */
bool wireless_busy(void);

/* Draws the connection and battery indicators */
void wireless_indicators(uint8_t led_min, uint8_t led_max);
