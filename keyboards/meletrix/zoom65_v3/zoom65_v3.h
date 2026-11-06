// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef union {
    uint32_t raw;
    struct {
        uint8_t wireless_device : 3;
        uint8_t screen_mode : 3;
        uint8_t screen_accent : 3;
        uint8_t screen_units : 2;
        uint8_t screen_clock_face : 2;
        uint8_t reserved : 1;
        uint8_t screen_custom : 1;
        uint8_t screen_media : 2;
    };
} kb_config_t;

extern kb_config_t kb_config;

kb_config_t kb_config_load(void);
void        kb_config_save(void);

void kb_led_power(bool on);
bool kb_cable_connected(void);
bool kb_battery_full(void);
