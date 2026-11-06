// SPDX-License-Identifier: GPL-2.0-or-later
#include QMK_KEYBOARD_H
#include "screen.h"
#ifdef RAW_ENABLE
#    include "raw_hid.h"
#endif
#include "zoom65_v3.h"
#ifdef LCDFW_ENABLE
#    include "lcdfw/lcdfw.h"
#endif
#if defined(LCDFW_ENABLE) || defined(SCREEN_PROBE_ENABLE)
#    include "screen_probe/screen_probe.h"
#endif

#define SCREEN_BAUD 115200

#define FRAME_START 0xA5
#define GROUP_CONTROL 0x00
#define GROUP_DATA 0x01
#define GROUP_MEDIA 0x02

#define CONTROL_BACKLIGHT 0x10
#define CONTROL_NEXT_PAGE 0x20
#define CONTROL_DOWN 0x21
#define CONTROL_UP 0x22
#define CONTROL_BOOTLOADER 0xFE
#define CONTROL_RESET 0xFF
#define DATA_BATTERY 0x70

#define HOST_REPORT_ID 0x58
#define HOST_REPORT_LEN 32

/* A flash erase can hold up a media frame's reply */
#define REPLY_TIMEOUT_MS 150
#define REPLY_TIMEOUT_MEDIA_MS 2000

/* Leaves the screen to the host for a while after it last sent something */
#define BATTERY_HOST_QUIET_MS 10000

#define BATTERY_UNSENT 0xFF

static const SerialConfig serial_config = {
    SCREEN_BAUD, UART_WRDLEN, UART_STPBIT, UART_PARITY, UART_ATFLCT,
};

/* State of the stock firmware, which restarts with its backlight on */
static bool    backlight_on = true;
static bool    user_off;
static uint8_t battery_sent = BATTERY_UNSENT;

#ifdef RAW_ENABLE
static bool     reply_pending;
static uint16_t reply_timeout;
static uint32_t reply_timer;
#endif
static uint32_t host_timer;

static bool screen_uart_owned_elsewhere(void) {
#if defined(LCDFW_ENABLE) || defined(SCREEN_PROBE_ENABLE)
    return screen_probe_active();
#else
    return false;
#endif
}

bool screen_custom_enabled(void) {
#ifdef LCDFW_ENABLE
    return kb_config.screen_custom;
#else
    return false;
#endif
}

static void reset_stock_state(void) {
    backlight_on = true;
    user_off     = false;
    battery_sent = BATTERY_UNSENT;
}

void screen_set_custom(bool on) {
#ifdef LCDFW_ENABLE
    if (on == screen_custom_enabled()) {
        return;
    }
    kb_config.screen_custom = on;
    kb_config_save();
    if (on) {
        lcdfw_start();
    } else {
        lcdfw_stop();
        reset_stock_state();
    }
#else
    (void)on;
#endif
}

static void send_control(uint8_t command) {
    if (!screen_uart_owned_elsewhere()) {
        const uint8_t frame[] = {FRAME_START, GROUP_CONTROL, command};
        sdWrite(&SD3, frame, sizeof(frame));
    }
}

static void set_backlight(bool on) {
    if (backlight_on != on) {
        backlight_on = on;
        send_control(CONTROL_BACKLIGHT);
    }
}

void screen_init(void) {
    palSetLineMode(SD3_TX_PIN, PAL_MODE_ALTERNATE(UART_TX_PAL_MODE) | PAL_OUTPUT_TYPE_PUSHPULL | PAL_OUTPUT_SPEED_HIGHEST);
    palSetLineMode(SD3_RX_PIN, PAL_MODE_ALTERNATE(UART_RX_PAL_MODE) | PAL_OUTPUT_TYPE_PUSHPULL | PAL_OUTPUT_SPEED_HIGHEST);
    sdStart(&SD3, &serial_config);

#ifdef LCDFW_ENABLE
    lcdfw_init(kb_config.screen_mode, kb_config.screen_accent, kb_config.screen_units, kb_config.screen_clock_face, kb_config.screen_media);
    if (screen_custom_enabled()) {
        lcdfw_start();
    }
#elif !defined(SCREEN_PROBE_ENABLE)
    gpio_set_pin_output(SCREEN_BOOT0_PIN);
    gpio_write_pin_low(SCREEN_BOOT0_PIN);
    gpio_set_pin_output(SCREEN_NRST_PIN);
    gpio_write_pin_high(SCREEN_NRST_PIN);
#endif
}

#ifdef RAW_ENABLE
static void send_reply(uint8_t value) {
    uint8_t report[HOST_REPORT_LEN] = {HOST_REPORT_ID, 1, value};

    reply_pending = false;
    raw_hid_send(report, sizeof(report));
}
#endif

void screen_task(void) {
#ifdef LCDFW_ENABLE
    lcdfw_task();
#endif
    if (screen_uart_owned_elsewhere()) {
        return;
    }

    /* The host waits for one reply per command, and anything else on the line is not for it */
    msg_t byte;
    while ((byte = sdGetTimeout(&SD3, TIME_IMMEDIATE)) >= MSG_OK) {
#ifdef RAW_ENABLE
        if (reply_pending) {
            send_reply(screen_custom_enabled() ? byte != 0 : byte);
        }
#endif
    }

#ifdef RAW_ENABLE
    if (reply_pending && timer_elapsed32(reply_timer) >= reply_timeout) {
        send_reply(0);
    }
#endif
}

void screen_set_power(bool on) {
#ifdef LCDFW_ENABLE
    if (screen_custom_enabled()) {
        lcdfw_set_power(on);
        return;
    }
#endif
    if (!user_off) {
        set_backlight(on);
    }
}

void screen_set_battery(uint8_t level) {
    if (screen_custom_enabled() || level == battery_sent || screen_uart_owned_elsewhere() || timer_elapsed32(host_timer) < BATTERY_HOST_QUIET_MS) {
        return;
    }
    battery_sent          = level;
    const uint8_t frame[] = {FRAME_START, GROUP_DATA, DATA_BATTERY, level};
    sdWrite(&SD3, frame, sizeof(frame));
}

#ifdef LCDFW_ENABLE
static bool process_custom_key(uint16_t keycode, bool pressed) {
    switch (keycode) {
        case SC_DOWN:
            if (pressed) {
                kb_config.screen_mode = lcdfw_next_mode();
                kb_config_save();
            }
            return true;
        case SC_UP:
            if (pressed) {
                kb_config.screen_mode = lcdfw_prev_mode();
                kb_config_save();
            }
            return true;
        case SC_SWCH:
            if (pressed) {
                uint8_t accent = kb_config.screen_accent;
                uint8_t units  = kb_config.screen_units;
                uint8_t face   = kb_config.screen_clock_face;
                uint8_t media  = kb_config.screen_media;
                if (lcdfw_page_setting(&accent, &units, &face, &media)) {
                    kb_config.screen_accent     = accent;
                    kb_config.screen_units      = units;
                    kb_config.screen_clock_face = face;
                    kb_config.screen_media      = media;
                    kb_config_save();
                }
            }
            return true;
        case SC_TOGG:
            if (pressed) {
                lcdfw_toggle_backlight();
            }
            return true;
        case SC_RST:
        case SC_BOOT:
            return true;
        default:
            return false;
    }
}
#endif

bool screen_process_key(uint16_t keycode, bool pressed) {
    uint8_t command;

    if (keycode == SC_FRMW) {
        if (pressed) {
            screen_set_custom(!screen_custom_enabled());
        }
        return true;
    }

#ifdef LCDFW_ENABLE
    if (screen_custom_enabled()) {
        return process_custom_key(keycode, pressed);
    }
#endif

    switch (keycode) {
        case SC_TOGG:
            if (pressed) {
                user_off = !user_off;
                set_backlight(!user_off);
            }
            return true;
        case SC_SWCH:
            command = CONTROL_NEXT_PAGE;
            break;
        case SC_DOWN:
            command = CONTROL_DOWN;
            break;
        case SC_UP:
            command = CONTROL_UP;
            break;
        case SC_BOOT:
            command = CONTROL_BOOTLOADER;
            break;
        case SC_RST:
            if (pressed) {
                reset_stock_state();
            }
            command = CONTROL_RESET;
            break;
        default:
            return false;
    }
    if (pressed) {
        send_control(command);
    }
    return true;
}

void screen_host_command(const uint8_t *frame, uint8_t len) {
    host_timer = timer_read32();

#ifdef RAW_ENABLE
    if (screen_uart_owned_elsewhere() || len == 0) {
        send_reply(0);
        return;
    }

    while (sdGetTimeout(&SD3, TIME_IMMEDIATE) != MSG_TIMEOUT) {
    }
    const bool media = frame[0] != FRAME_START || (len > 1 && frame[1] == GROUP_MEDIA) || len > 8;
    sdWrite(&SD3, frame, len);

    reply_pending = true;
    reply_timeout = media ? REPLY_TIMEOUT_MEDIA_MS : REPLY_TIMEOUT_MS;
    reply_timer   = timer_read32();
#endif
}
