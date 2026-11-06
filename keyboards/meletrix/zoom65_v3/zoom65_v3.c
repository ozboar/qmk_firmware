// SPDX-License-Identifier: GPL-2.0-or-later
#include QMK_KEYBOARD_H
#include "usb_main.h"
#include "raw_hid.h"
#ifdef VIA_ENABLE
#    include "via.h"
#endif
#include "zoom65_v3.h"
#include "wireless/wireless.h"
#include "wireless/sleep.h"
#include "screen/screen.h"
#ifdef LCDFW_ENABLE
#    include "lcdfw/lcdfw.h"
#endif
#if defined(LCDFW_ENABLE) || defined(SCREEN_PROBE_ENABLE)
#    include "screen_probe/screen_probe.h"
#endif

#define SCREEN_HOST_REPORT_ID 0x58
#define SCREEN_HOST_FRAME_MAX 30
#define USB_REMOTE_WAKEUP_ENABLED 2U
#define VIA_VALUE_CUSTOM_SCREEN 1

kb_config_t kb_config;

kb_config_t kb_config_load(void) {
    kb_config.raw = eeconfig_read_kb();
    return kb_config;
}

void kb_config_save(void) {
    eeconfig_update_kb(kb_config.raw);
}

/* The data pin lets go while the rail is off, or the LED updates would feed power into the LEDs */
void kb_led_power(bool on) {
    if (on) {
        gpio_write_pin_low(WS2812_DI_PIN);
        gpio_set_pin_output(WS2812_DI_PIN);
    } else {
        gpio_set_pin_input_low(WS2812_DI_PIN);
    }
    gpio_write_pin(LED_POWER_PIN, !on);
    gpio_write_pin(LED_POWER_AUX_PIN, !on);
}

void kb_led_update(void) {
    static bool powered = true;

    /* QMK only draws indicators when an effect is running, so nothing can light without one */
    const bool wanted = !sleep_leds_off() && rgb_matrix_is_enabled() && rgb_matrix_get_mode() != RGB_MATRIX_NONE && !rgb_matrix_get_suspend_state();

    if (wanted != powered) {
        powered = wanted;
        kb_led_power(wanted);
    }
}

bool kb_cable_connected(void) {
    return gpio_read_pin(CABLE_DETECT_PIN);
}

bool kb_battery_full(void) {
    return gpio_read_pin(BATTERY_FULL_PIN);
}

void eeconfig_init_kb(void) {
    kb_config.raw             = 0;
    kb_config.wireless_device = WL_DEV_BT1;
    kb_config.screen_custom   = true;
    kb_config_save();
    eeconfig_init_user();
}

void keyboard_pre_init_kb(void) {
    gpio_set_pin_input(CABLE_DETECT_PIN);
    gpio_set_pin_input(BATTERY_FULL_PIN);
    gpio_set_pin_input(WIRELESS_SWITCH_PIN);

    /* Levels first, so the pins never glitch when they become outputs */
    gpio_write_pin_low(USB_DATA_DISABLE_PIN);
    gpio_set_pin_output(USB_DATA_DISABLE_PIN);

    kb_led_power(true);
    gpio_set_pin_output(LED_POWER_PIN);
    gpio_set_pin_output(LED_POWER_AUX_PIN);

    keyboard_pre_init_user();
}

void keyboard_post_init_kb(void) {
    screen_init();
    keyboard_post_init_user();
}

void suspend_power_down_kb(void) {
    screen_set_power(false);
    suspend_power_down_user();
}

void suspend_wakeup_init_kb(void) {
    screen_set_power(true);
    suspend_wakeup_init_user();
}

/* Bluetooth support turns off the blocking suspend loop, which would also hold up a move to wireless */
static void usb_suspend_task(void) {
    static uint32_t suspend_timer;
    static bool     suspended;

    if (wireless_device() != WL_DEV_USB || USB_DRIVER.state != USB_SUSPENDED) {
        suspend_timer = 0;
        if (suspended) {
            suspended = false;
            if (wireless_device() != WL_DEV_USB) {
                suspend_wakeup_init();
            }
        }
        return;
    }

    if (!suspend_timer) {
        suspend_timer = timer_read32() | 1;
    }
    if (timer_elapsed32(suspend_timer) >= USB_SUSPEND_DELAY_MS) {
        suspended = true;
        suspend_power_down();
        if ((USB_DRIVER.status & USB_REMOTE_WAKEUP_ENABLED) && suspend_wakeup_condition()) {
            usbWakeupHost(&USB_DRIVER);
        }
    }
}

void housekeeping_task_kb(void) {
    screen_task();
    usb_suspend_task();
    kb_led_update();
    housekeeping_task_user();
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    const bool pressed = record->event.pressed;

#ifdef LCDFW_ENABLE
    if (pressed) {
        lcdfw_on_keypress(record->event.key.row, record->event.key.col);
    }
#endif
    if (!process_record_user(keycode, record)) {
        return false;
    }

    switch (keycode) {
        case WL_BT1:
        case BT_PRF1:
            wireless_device_key(WL_DEV_BT1, pressed);
            return false;
        case WL_BT2:
        case BT_PRF2:
            wireless_device_key(WL_DEV_BT2, pressed);
            return false;
        case WL_BT3:
        case BT_PRF3:
            wireless_device_key(WL_DEV_BT3, pressed);
            return false;
        case BT_PRF4:
            wireless_device_key(WL_DEV_BT4, pressed);
            return false;
        case BT_PRF5:
            wireless_device_key(WL_DEV_BT5, pressed);
            return false;
        case WL_2G4:
        case OU_2P4G:
            wireless_device_key(WL_DEV_2G4, pressed);
            return false;
        case WL_USB:
        case OU_USB:
            wireless_device_key(WL_DEV_USB, pressed);
            return false;
        case OU_BT:
            if (pressed) {
                wireless_resume();
            }
            return false;
        /* Only USB or the selected wireless device are valid hosts */
        case OU_AUTO:
        case OU_NONE:
        case OU_NEXT:
        case OU_PREV:
            return false;
        case WL_BATT:
            wireless_battery_key(pressed);
            return false;
        default:
            return !screen_process_key(keycode, pressed);
    }
}

bool rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max) {
    if (!rgb_matrix_indicators_advanced_user(led_min, led_max)) {
        return false;
    }

    wireless_indicators(led_min, led_max);

    if (host_keyboard_led_state().caps_lock) {
        RGB_MATRIX_INDICATOR_SET_COLOR(CAPS_INDEX, 0x77, 0x77, 0x77);
    }
    if (keymap_config.no_gui) {
        RGB_MATRIX_INDICATOR_SET_COLOR(GUI_INDEX, 255, 255, 255);
    }
    return true;
}

#ifdef RAW_ENABLE
/* Commands for the screen, on the same raw HID interface as VIA */
bool via_command_kb(uint8_t *data, uint8_t length) {
#    ifdef SCREEN_PROBE_ENABLE
    if (data[0] == SCREEN_PROBE_REPORT_ID) {
        uint8_t reply[RAW_EPSIZE];
        screen_probe_command(data, reply);
        raw_hid_send(reply, sizeof(reply));
        screen_probe_stream(raw_hid_send);
        return true;
    }
#    endif
    if (data[0] == SCREEN_HOST_REPORT_ID && data[1] <= SCREEN_HOST_FRAME_MAX) {
        screen_host_command(data + 2, data[1]);
        return true;
    }
    return false;
}

#    ifdef VIA_ENABLE
void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    uint8_t *const command = &data[0];

    if (data[1] != id_custom_channel || data[2] != VIA_VALUE_CUSTOM_SCREEN) {
        *command = id_unhandled;
        return;
    }

    switch (*command) {
        case id_custom_set_value:
            screen_set_custom(data[3]);
            break;
        case id_custom_get_value:
            data[3] = screen_custom_enabled();
            break;
        case id_custom_save:
            /* Already saved when set */
            break;
        default:
            *command = id_unhandled;
            break;
    }
}
#    endif

#    ifndef VIA_ENABLE
__attribute__((weak)) bool raw_hid_receive_user(uint8_t *data, uint8_t length) {
    return false;
}

void raw_hid_receive(uint8_t *data, uint8_t length) {
    if (raw_hid_receive_user(data, length) || via_command_kb(data, length)) {
        return;
    }
    data[0] = 0;
    raw_hid_send(data, length);
}
#    endif
#endif
