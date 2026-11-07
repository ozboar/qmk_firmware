// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "bluetooth.h"
#include "connection.h"
#include "usb_main.h"
#include "usb_util.h"
#include "zoom65_v3.h"
#include "module.h"
#include "sleep.h"
#include "wireless.h"
#include "screen/screen.h"

/* The module takes this long to boot before it accepts the channel */
#define BOOT_DELAY_MS 2000
#define BATTERY_POLL_MS 10000
#define PAIR_HOLD_MS 3000

#define PAIR_TIMEOUT_MS 60000
#define PAIR_BLINK_MS 200
#define RECONNECT_TIMEOUT_MS 30000
#define RECONNECT_BLINK_MS 500
#define CONNECTED_SHOW_MS 3000

/* The same name as stock firmware. The $ looks like a placeholder for the profile number */
#define MODULE_NAME "Zoom65 v3 BT$"

/* The Q, W, E and R keys */
#define BT1_LED 15
#define G24_LED 18
/* The A and S keys, whose LEDs differ between layouts */
#define BT4_LED g_led_config.matrix_co[2][1]
#define BT5_LED g_led_config.matrix_co[2][2]

#define BATTERY_BAR_FIRST_LED 1
#define BATTERY_GOOD_PERCENT 50
#define BATTERY_LOW_PERCENT 30
#define BATTERY_BAR_LEDS 10
#define BATTERY_WARN_PERCENT 10
#define BATTERY_WARN_PERIOD_MS 30000
#define BATTERY_WARN_PULSE_MS 1000

typedef enum {
    LINK_IDLE,
    LINK_PAIRING,
    LINK_RECONNECTING,
    LINK_CONNECTED,
} link_state_t;

static wl_dev_t last_wireless = WL_DEV_BT1;
static bool     host_is_usb   = true;

static bool     booted;
static uint32_t boot_timer;
static uint32_t battery_timer;

static wl_dev_t held_device = WL_DEV_USB;
static uint32_t held_timer;
static bool     show_battery;

static link_state_t link_state;
static uint32_t     link_timer;
static uint32_t     blink_timer;
static bool         blink_on;
static bool         gave_up;

/* Boot keyboard report, whose slots hold the first keys pressed, then bits for the rest */
static uint8_t  tx_keyboard[MODULE_KEYBOARD_LEN];
static uint8_t  tx_nkro[MODULE_NKRO_LEN];
static uint8_t  tx_mouse[MODULE_MOUSE_LEN];
static uint16_t tx_consumer;
static uint8_t  tx_system;

enum {
    DIRTY_KEYBOARD = 1 << 0,
    DIRTY_NKRO     = 1 << 1,
    DIRTY_CONSUMER = 1 << 2,
    DIRTY_SYSTEM   = 1 << 3,
    DIRTY_MOUSE    = 1 << 4,
};
static uint8_t dirty;

static void flush_reports(void) {
    if ((dirty & DIRTY_KEYBOARD) && module_send_keyboard(tx_keyboard)) dirty &= ~DIRTY_KEYBOARD;
    if ((dirty & DIRTY_NKRO) && module_send_nkro(tx_nkro)) dirty &= ~DIRTY_NKRO;
    if ((dirty & DIRTY_CONSUMER) && module_send_consumer(tx_consumer)) dirty &= ~DIRTY_CONSUMER;
    if ((dirty & DIRTY_SYSTEM) && module_send_system(tx_system)) dirty &= ~DIRTY_SYSTEM;
    if ((dirty & DIRTY_MOUSE) && module_send_mouse(tx_mouse)) dirty &= ~DIRTY_MOUSE;
}

static uint8_t channel_command(wl_dev_t device) {
    switch (device) {
        case WL_DEV_BT1:
            return MODULE_CMD_BT1;
        case WL_DEV_BT2:
            return MODULE_CMD_BT2;
        case WL_DEV_BT3:
            return MODULE_CMD_BT3;
        case WL_DEV_BT4:
            return MODULE_CMD_BT4;
        case WL_DEV_BT5:
            return MODULE_CMD_BT5;
        case WL_DEV_2G4:
            return MODULE_CMD_2G4;
        default:
            return MODULE_CMD_USB;
    }
}

static void set_link_state(link_state_t state) {
    link_state  = state;
    link_timer  = timer_read32();
    blink_timer = 0;
    blink_on    = false;
}

static void select_device(wl_dev_t device, bool pair) {
    const bool usb = device == WL_DEV_USB;

    module_reset_link();
    gave_up = false;

    if (!usb && device != last_wireless) {
        last_wireless             = device;
        kb_config.wireless_device = device;
        kb_config_save();
    }
    connection_set_host(usb ? CONNECTION_HOST_USB : CONNECTION_HOST_BLUETOOTH);

    if (usb) {
        set_link_state(LINK_IDLE);
    } else if (pair) {
        set_link_state(LINK_PAIRING);
        module_send_control(MODULE_CMD_PAIR);
        return;
    } else {
        set_link_state(LINK_RECONNECTING);
    }
    module_send_control(channel_command(device));
}

void wireless_device_key(wl_dev_t device, bool pressed) {
    if (!pressed) {
        held_device = WL_DEV_USB;
        return;
    }
    if (device != wireless_device()) {
        select_device(device, false);
    }
    if (device != WL_DEV_USB) {
        held_device = device;
        held_timer  = timer_read32();
    }
}

void wireless_resume(void) {
    if (wireless_device() == WL_DEV_USB) {
        select_device(last_wireless, false);
    }
}

void wireless_battery_key(bool pressed) {
    show_battery = pressed;
    if (pressed) {
        module_send_control(MODULE_CMD_BATTERY);
    }
}

wl_dev_t wireless_device(void) {
    return connection_get_host_raw() == CONNECTION_HOST_USB ? WL_DEV_USB : last_wireless;
}

uint8_t wireless_battery(void) {
    return module_battery();
}

bool wireless_should_sleep(void) {
    return gave_up || module_battery_critical();
}

void wireless_on_wake(void) {
    gave_up = false;
    module_clear_battery_critical();
}

bool wireless_link_active(void) {
    return link_state == LINK_PAIRING || link_state == LINK_RECONNECTING;
}

bool wireless_busy(void) {
    return dirty || module_busy();
}

static void link_task(void) {
    if (wireless_device() == WL_DEV_USB) {
        return;
    }

    switch (link_state) {
        case LINK_PAIRING:
        case LINK_RECONNECTING: {
            const bool pairing = link_state == LINK_PAIRING;
            if (module_connected()) {
                set_link_state(LINK_CONNECTED);
            } else if (timer_elapsed32(link_timer) >= (pairing ? PAIR_TIMEOUT_MS : RECONNECT_TIMEOUT_MS)) {
                set_link_state(LINK_IDLE);
                gave_up = true;
            } else if (blink_timer == 0 || timer_elapsed32(blink_timer) >= (pairing ? PAIR_BLINK_MS : RECONNECT_BLINK_MS)) {
                blink_timer = timer_read32() | 1;
                blink_on    = !blink_on;
            }
            break;
        }
        case LINK_CONNECTED:
            if (!module_connected() || timer_elapsed32(link_timer) >= CONNECTED_SHOW_MS) {
                set_link_state(LINK_IDLE);
            }
            break;
        case LINK_IDLE:
            if (!module_connected() && !gave_up) {
                set_link_state(LINK_RECONNECTING);
                module_send_control(channel_command(last_wireless));
            }
            break;
    }
}

static void pair_hold_task(void) {
    if (held_device != WL_DEV_USB && timer_elapsed32(held_timer) >= PAIR_HOLD_MS) {
        select_device(held_device, true);
        held_device = WL_DEV_USB;
    }
}

static uint8_t battery_screen_level(void) {
    if (kb_cable_connected()) {
        return 0;
    }
    const uint8_t level = (module_battery() + 10) / 25 + 1;
    return level > 5 ? 5 : level;
}

static void boot_task(void) {
    if (booted || timer_elapsed32(boot_timer) < BOOT_DELAY_MS) {
        return;
    }
    booted = true;
    module_send_name(MODULE_NAME);
    if (wireless_device() == WL_DEV_USB) {
        module_send_control(MODULE_CMD_USB);
    } else {
        set_link_state(LINK_RECONNECTING);
        module_send_control(channel_command(last_wireless));
    }
    module_send_control(MODULE_CMD_BT_SLEEP);
}

void bluetooth_init(void) {
    const uint8_t stored = kb_config_load().wireless_device;

    if ((stored >= WL_DEV_BT1 && stored <= WL_DEV_BT5) || stored == WL_DEV_2G4) {
        last_wireless = stored;
    }
    /* Only USB and the module are ever valid here, whatever older settings left behind */
    if (connection_get_host_raw() != CONNECTION_HOST_BLUETOOTH) {
        connection_set_host(CONNECTION_HOST_USB);
    }
    module_init();
    boot_timer = timer_read32();
}

void bluetooth_task(void) {
    flush_reports();
    module_task();
    boot_task();

    if (!booted) {
        return;
    }
    if (timer_elapsed32(battery_timer) >= BATTERY_POLL_MS) {
        battery_timer = timer_read32();
        module_send_control(MODULE_CMD_BATTERY);
    }
    screen_set_battery(battery_screen_level());
    pair_hold_task();
    link_task();
    sleep_task();
}

bool bluetooth_is_connected(void) {
    return module_connected();
}

bool bluetooth_can_send_nkro(void) {
    return true;
}

uint8_t bluetooth_keyboard_leds(void) {
    return module_leds();
}

void bluetooth_send_keyboard(report_keyboard_t *report) {
    tx_keyboard[0] = report->mods;
    memcpy(tx_keyboard + 2, report->keys, KEYBOARD_REPORT_KEYS);
    dirty |= DIRTY_KEYBOARD;
    flush_reports();
}

/* Keeps held keys in their slots, and gives high usages first claim as only they miss the bitmap */
void bluetooth_send_nkro(report_nkro_t *report) {
    uint8_t *const slots = tx_keyboard + 2;
    uint8_t        report_new[MODULE_KEYBOARD_LEN];
    uint8_t        nkro_new[MODULE_NKRO_LEN] = {0};

    memcpy(report_new, tx_keyboard, sizeof(report_new));
    uint8_t *const slots_new = report_new + 2;
    report_new[0]            = report->mods;

    for (uint8_t i = 0; i < KEYBOARD_REPORT_KEYS; i++) {
        const uint8_t usage = slots[i];
        if (usage && !(report->bits[usage >> 3] & (1 << (usage & 7)))) {
            slots_new[i] = 0;
        }
    }

    for (int usage = NKRO_REPORT_BITS * 8 - 1; usage >= 0; usage--) {
        if (!(report->bits[usage >> 3] & (1 << (usage & 7)))) {
            continue;
        }
        int8_t free_slot = -1;
        bool   slotted   = false;
        for (uint8_t i = 0; i < KEYBOARD_REPORT_KEYS; i++) {
            if (slots_new[i] == usage) {
                slotted = true;
                break;
            }
            if (!slots_new[i] && free_slot < 0) {
                free_slot = i;
            }
        }
        if (slotted) {
            continue;
        }
        if (free_slot >= 0) {
            slots_new[free_slot] = usage;
        } else if (usage < MODULE_NKRO_LEN * 8) {
            nkro_new[usage >> 3] |= 1 << (usage & 7);
        }
    }

    if (memcmp(report_new, tx_keyboard, sizeof(report_new)) != 0) {
        memcpy(tx_keyboard, report_new, sizeof(report_new));
        dirty |= DIRTY_KEYBOARD;
    }
    if (memcmp(nkro_new, tx_nkro, sizeof(nkro_new)) != 0) {
        memcpy(tx_nkro, nkro_new, sizeof(nkro_new));
        dirty |= DIRTY_NKRO;
    }
    flush_reports();
}

void bluetooth_send_mouse(report_mouse_t *report) {
    tx_mouse[0] = report->buttons;
    tx_mouse[1] = report->x;
    tx_mouse[2] = report->y;
    tx_mouse[3] = report->v;
    tx_mouse[4] = report->h;
    dirty |= DIRTY_MOUSE;
    flush_reports();
}

void bluetooth_send_consumer(uint16_t usage) {
    tx_consumer = usage;
    dirty |= DIRTY_CONSUMER;
    flush_reports();
}

/* The module takes power, sleep and wake as bits 0 to 2 */
void bluetooth_send_system(uint16_t usage) {
    tx_system = usage ? 1 << (usage - SYSTEM_POWER_DOWN) : 0;
    dirty |= DIRTY_SYSTEM;
    flush_reports();
}

/* The module only carries raw HID to the dongle */
void bluetooth_send_raw_hid(uint8_t *data, uint8_t length) {
    if (length == MODULE_RAW_LEN && wireless_device() == WL_DEV_2G4) {
        module_send_raw(data);
    }
}

void host_connect_active_driver_kb(connection_host_t host) {
    if (host == CONNECTION_HOST_USB) {
        gpio_write_pin_low(USB_DATA_DISABLE_PIN);
        if (!host_is_usb) {
            restart_usb_driver(&USB_DRIVER);
            host_is_usb = true;
        }
    } else if (host == CONNECTION_HOST_BLUETOOTH) {
        if (host_is_usb) {
            usb_disconnect();
            host_is_usb = false;
        }
        gpio_write_pin_high(USB_DATA_DISABLE_PIN);
    }
}

void wireless_indicators(uint8_t led_min, uint8_t led_max) {
    const wl_dev_t device = wireless_device();

    if (device == WL_DEV_USB) {
        return;
    }

    if (show_battery) {
        const uint8_t percent = module_battery();
        const uint8_t red     = percent <= BATTERY_GOOD_PERCENT ? 255 : 0;
        const uint8_t green   = percent > BATTERY_LOW_PERCENT ? 255 : 0;

        for (uint8_t led = led_min; led < led_max; led++) {
            rgb_matrix_set_color(led, 0, 0, 0);
        }
        for (uint8_t i = 0; i < percent / 10; i++) {
            RGB_MATRIX_INDICATOR_SET_COLOR(BATTERY_BAR_FIRST_LED + i, red, green, 0);
        }
    } else if (!kb_cable_connected() && module_battery() <= BATTERY_WARN_PERCENT) {
        const uint32_t now = timer_read32() % BATTERY_WARN_PERIOD_MS;
        if (now < BATTERY_WARN_PULSE_MS) {
            const uint32_t ramp = now < BATTERY_WARN_PULSE_MS / 2 ? now : BATTERY_WARN_PULSE_MS - now;
            const uint8_t  red  = ramp * 2 * RGB_MATRIX_MAXIMUM_BRIGHTNESS / BATTERY_WARN_PULSE_MS;
            for (uint8_t i = 0; i < BATTERY_BAR_LEDS; i++) {
                RGB_MATRIX_INDICATOR_SET_COLOR(BATTERY_BAR_FIRST_LED + i, red, 0, 0);
            }
        }
    }

    if ((link_state == LINK_CONNECTED) || blink_on) {
        const uint8_t led = device == WL_DEV_2G4 ? G24_LED : device == WL_DEV_BT4 ? BT4_LED : device == WL_DEV_BT5 ? BT5_LED : BT1_LED + device - WL_DEV_BT1;
        if (device == WL_DEV_2G4) {
            RGB_MATRIX_INDICATOR_SET_COLOR(led, 255, 0, 0);
        } else {
            RGB_MATRIX_INDICATOR_SET_COLOR(led, 0, 0, 255);
        }
    }
}
