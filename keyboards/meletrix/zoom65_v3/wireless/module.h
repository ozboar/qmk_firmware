// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Device control commands sent to the module */
enum {
    MODULE_CMD_USB      = 0x11,
    MODULE_CMD_2G4      = 0x30,
    MODULE_CMD_BT1      = 0x31,
    MODULE_CMD_BT2      = 0x32,
    MODULE_CMD_BT3      = 0x33,
    MODULE_CMD_PAIR     = 0x51,
    MODULE_CMD_BATTERY  = 0x53,
    MODULE_CMD_BT_SLEEP = 0x55,
};

#define MODULE_KEYBOARD_LEN 8
#define MODULE_NKRO_LEN 14
#define MODULE_MOUSE_LEN 5
#define MODULE_RAW_LEN 32

void module_init(void);
void module_task(void);

/* Each returns false if the transmit queue is full, and sends as soon as the link is free */
bool module_send_control(uint8_t command);
bool module_send_keyboard(const uint8_t *report);
bool module_send_nkro(const uint8_t *bits);
bool module_send_consumer(uint16_t usage);
bool module_send_system(uint8_t bits);
bool module_send_mouse(const uint8_t *report);
bool module_send_name(const char *name);
bool module_send_raw(const uint8_t *data);

/* Forget the link state, used when moving to another device */
void module_reset_link(void);

bool    module_connected(void);
uint8_t module_battery(void);
uint8_t module_leds(void);

/* Set when the module reports the battery is about to cut out, until cleared or it recovers */
bool module_battery_critical(void);
void module_clear_battery_critical(void);

/* True while frames are still waiting to go out */
bool module_busy(void);
