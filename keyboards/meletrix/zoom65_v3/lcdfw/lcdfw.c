#include "quantum.h"
#include "version.h"
#ifdef VIA_ENABLE
#    include "via.h"
#endif
#include "zoom65_v3.h"
#include "wireless/wireless.h"
#include "lcdfw.h"
#include "lcdfw_load.h"
#include "screen_probe/screen_probe.h"

#if defined(KEYBOARD_meletrix_zoom65_v3_iso) || defined(KEYBOARD_meletrix_zoom65_v3_iso_tl)
#    define LCDFW_ISO 1
#else
#    define LCDFW_ISO 0
#endif

#define FRAME_START 0xA5
#define GROUP_UI 0x03
#define TASK_INTERVAL_MS 20
#define REFRESH_INTERVAL_MS 1000
#define MODE_COUNT 8
#define MEDIA_SETTING_COUNT 3
#define RGB_NAME_SIZE 20
#define BUILD_FIELD_SIZE 10
#define QMK_VERSION_SIZE 18
#ifdef VIA_ENABLE
#    define LCDFW_VIA_VERSION VIA_PROTOCOL_VERSION
#else
#    define LCDFW_VIA_VERSION 0
#endif
#define MODE_CLOCK 0
#define MODE_CAT 2
#define MODE_GRAPH 3
#define MODE_SYSTEM 4
#define MODE_WEATHER 5
#define MODE_MEDIA 1
#define MODE_INFO 7
#define CLOCK_FACE_COUNT 4
#define UNITS_SYSTEM_F (1u << 0)
#define UNITS_WEATHER_F (1u << 1)
#define ACCENT_COUNT 7
#define BRIGHTNESS_FLOOR 5
#define BRIGHTNESS_DIM_DIVISOR 4
#define WAKE_RETRY_MS 20
#define WAKE_ATTEMPTS 10
#define WAKE_READY 0x5A

enum {
    UI_TAP = 0x01,
    UI_WPM = 0x02,
    UI_STATUS = 0x03,
    UI_KEYS = 0x04,
    UI_MODE = 0x05,
    UI_POWER = 0x06,
    UI_BACKLIGHT = 0x07,
    UI_LAYOUT = 0x08,
    UI_KEY_TAP = 0x09,
    UI_ACCENT = 0x0A,
    UI_UNITS = 0x0B,
    UI_STATUS_EXTRA = 0x0C,
    UI_RGB = 0x0D,
    UI_RGB_NAME = 0x0E,
    UI_CLOCK_FACE = 0x0F,
    UI_BUILD = 0x10,
    UI_VERSIONS = 0x11,
    UI_MEDIA = 0x12,
    UI_BRIGHTNESS = 0x13,
    UI_WAKE = 0x14,
};

static uint8_t      mode;
static uint8_t      accent;
static uint8_t      units;
static uint8_t      clock_face;
static uint8_t      media;
static bool         backlight = true;
static bool         powered   = true;
static bool         waking;
static bool         dimmed;
static uint8_t      wake_attempts;
static uint32_t     wake_timer;
static uint8_t      brightness_sent;
static bool         enabled;
static bool         state_sent;
static uint32_t     task_timer;
static uint32_t     refresh_timer;
static uint16_t     wpm_sent;
static uint8_t      status_sent[4];
static uint8_t      extra_sent[1];
static uint8_t      rgb_sent[6];

/* Effect names, in the same order as QMK's effect numbering */
static const char *const rgb_effect_names[] = {
    "NONE",
#define RGB_MATRIX_EFFECT(name, ...) #name,
#include "rgb_matrix_effects.inc"
#undef RGB_MATRIX_EFFECT
#if defined(RGB_MATRIX_CUSTOM_KB) || defined(RGB_MATRIX_CUSTOM_USER)
#    define RGB_MATRIX_EFFECT(name, ...) #name,
#    ifdef RGB_MATRIX_CUSTOM_KB
#        include "rgb_matrix_kb.inc"
#    endif
#    ifdef RGB_MATRIX_CUSTOM_USER
#        include "rgb_matrix_user.inc"
#    endif
#    undef RGB_MATRIX_EFFECT
#endif
};
static matrix_row_t keys_sent[MATRIX_ROWS];

/* Never blocks, so UI traffic cannot stall the matrix. A dropped frame is resent by the refresh */
static void send(uint8_t command, const uint8_t *payload, uint8_t len) {
    if (!enabled || screen_probe_active() || (lcdfw_asleep() && command != UI_POWER && command != UI_WAKE)) {
        return;
    }
    uint8_t frame[3 + RGB_NAME_SIZE] = {FRAME_START, GROUP_UI, command};
    memcpy(frame + 3, payload, len);
    chnWriteTimeout(&SD3, frame, 3 + len, TIME_IMMEDIATE);
}

static void send_u8(uint8_t command, uint8_t value) {
    send(command, &value, 1);
}

/* Follows the RGB brightness setting, but stays readable when that is at its lowest */
static uint8_t read_brightness(void) {
    uint8_t percent = MIN((uint16_t)rgb_matrix_get_val() * 100 / RGB_MATRIX_MAXIMUM_BRIGHTNESS, 100);

    if (dimmed) {
        percent /= BRIGHTNESS_DIM_DIVISOR;
    }
    return MAX(percent, BRIGHTNESS_FLOOR);
}

static void send_brightness(uint8_t percent) {
    send_u8(UI_BRIGHTNESS, percent);
    brightness_sent = percent;
}

static void send_wpm(uint16_t wpm) {
    const uint8_t payload[2] = {wpm >> 8, wpm & 0xFF};
    send(UI_WPM, payload, sizeof(payload));
    wpm_sent = wpm;
}

static void read_status(uint8_t *status) {
    status[0] = wireless_device();
    status[1] = get_highest_layer(layer_state | default_layer_state);
    status[2] = host_keyboard_led_state().caps_lock;
    status[3] = wireless_battery();
}

static void send_status(const uint8_t *status) {
    send(UI_STATUS, status, sizeof(status_sent));
    memcpy(status_sent, status, sizeof(status_sent));
}


static void read_extra(uint8_t *extra) {
    extra[0] = (kb_cable_connected() ? 1 : 0) | (kb_battery_full() ? 2 : 0) | (keymap_config.nkro ? 4 : 0);
}

static void read_rgb(uint8_t *rgb) {
    rgb[0] = rgb_matrix_is_enabled();
    rgb[1] = rgb_matrix_get_mode();
    rgb[2] = rgb_matrix_get_hue();
    rgb[3] = rgb_matrix_get_sat();
    rgb[4] = rgb_matrix_get_val();
    rgb[5] = rgb_matrix_get_speed();
}

static void send_rgb_name(uint8_t mode_number) {
    uint8_t name[RGB_NAME_SIZE] = {0};

    if (mode_number < sizeof(rgb_effect_names) / sizeof(rgb_effect_names[0])) {
        const char *source = rgb_effect_names[mode_number];
        for (uint8_t i = 0; i < RGB_NAME_SIZE - 1 && source[i]; i++) {
            name[i] = source[i] == '_' ? ' ' : source[i];
        }
    }
    send(UI_RGB_NAME, name, sizeof(name));
}

static void send_rgb(const uint8_t *rgb) {
    send(UI_RGB, rgb, sizeof(rgb_sent));
    if (rgb[1] != rgb_sent[1]) {
        send_rgb_name(rgb[1]);
    }
    memcpy(rgb_sent, rgb, sizeof(rgb_sent));
}

static void send_extra(const uint8_t *extra) {
    send(UI_STATUS_EXTRA, extra, sizeof(extra_sent));
    memcpy(extra_sent, extra, sizeof(extra_sent));
}

static void send_keys(uint8_t row, matrix_row_t state) {
    const uint8_t payload[3] = {row, (uint16_t)state >> 8, state & 0xFF};
    send(UI_KEYS, payload, sizeof(payload));
    keys_sent[row] = state;
}

/* Also recovers a screen that restarted, which comes up knowing nothing */
static void send_build(void) {
    uint8_t build[BUILD_FIELD_SIZE * 2] = {0};

    memcpy(build, QMK_BUILDDATE, BUILD_FIELD_SIZE);
    strncpy((char *)build + BUILD_FIELD_SIZE, LCDFW_BUILD_ID, BUILD_FIELD_SIZE);
    send(UI_BUILD, build, sizeof(build));
}

static void send_versions(void) {
    uint8_t versions[QMK_VERSION_SIZE + 2] = {0};

    strncpy((char *)versions, LCDFW_QMK_VERSION, QMK_VERSION_SIZE - 1);
    versions[QMK_VERSION_SIZE]     = LCDFW_VIA_VERSION >> 8;
    versions[QMK_VERSION_SIZE + 1] = LCDFW_VIA_VERSION & 0xFF;
    send(UI_VERSIONS, versions, sizeof(versions));
}

static void send_all(void) {
    uint8_t status[sizeof(status_sent)];

    send_u8(UI_LAYOUT, LCDFW_ISO);
    send_u8(UI_ACCENT, accent);
    send_u8(UI_UNITS, units);
    send_u8(UI_CLOCK_FACE, clock_face);
    send_u8(UI_MEDIA, media);
    send_build();
    send_versions();
    send_u8(UI_MODE, mode);
    send_brightness(read_brightness());
    send_u8(UI_BACKLIGHT, backlight);
    send_u8(UI_POWER, powered);
    read_status(status);
    send_status(status);
    uint8_t extra[sizeof(extra_sent)];
    read_extra(extra);
    send_extra(extra);
    uint8_t rgb[sizeof(rgb_sent)];
    read_rgb(rgb);
    send_rgb(rgb);
    send_rgb_name(rgb[1]);
    send_wpm(get_current_wpm());
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        send_keys(row, matrix_get_row(row));
    }
}

void lcdfw_init(uint8_t saved_mode, uint8_t saved_accent, uint8_t saved_units, uint8_t saved_clock_face, uint8_t saved_media) {
    media      = saved_media < MEDIA_SETTING_COUNT ? saved_media : 0;
    units      = saved_units;
    clock_face = saved_clock_face < CLOCK_FACE_COUNT ? saved_clock_face : 0;
    mode   = saved_mode < MODE_COUNT ? saved_mode : 0;
    accent = saved_accent < ACCENT_COUNT ? saved_accent : 0;
}

void lcdfw_start(void) {
    enabled    = true;
    state_sent = false;
    waking     = false;
    lcdfw_load_restart();
}

void lcdfw_stop(void) {
    enabled = false;
    waking  = false;
    lcdfw_load_stop();
}

/* The first frames are lost while the screen restarts its clocks, so keep asking */
static void wake_task(void) {
    msg_t byte;

    while ((byte = sdGetTimeout(&SD3, TIME_IMMEDIATE)) >= MSG_OK) {
        if (byte == WAKE_READY) {
            waking     = false;
            state_sent = false;
            return;
        }
    }
    if (timer_elapsed32(wake_timer) < WAKE_RETRY_MS) {
        return;
    }
    if (++wake_attempts >= WAKE_ATTEMPTS) {
        lcdfw_start();
        return;
    }
    wake_timer = timer_read32();
    send(UI_WAKE, (const uint8_t *)"", 0);
}

void lcdfw_task(void) {
    uint8_t status[sizeof(status_sent)];

    if (!enabled || lcdfw_load_task()) {
        return;
    }

    /* The screen shows nothing until it knows the mode, so send everything the moment it is up */
    if (!state_sent && lcdfw_load_done()) {
        state_sent    = true;
        refresh_timer = timer_read32();
        send_all();
        return;
    }

    if (waking) {
        wake_task();
        return;
    }
    if (!powered) {
        return;
    }

    if (timer_elapsed32(task_timer) < TASK_INTERVAL_MS) {
        return;
    }
    task_timer = timer_read32();

    if (timer_elapsed32(refresh_timer) >= REFRESH_INTERVAL_MS) {
        refresh_timer = timer_read32();
        send_all();
        return;
    }

    if (read_brightness() != brightness_sent) {
        send_brightness(read_brightness());
    }

    if (get_current_wpm() != wpm_sent) {
        send_wpm(get_current_wpm());
    }

    read_status(status);
    if (memcmp(status, status_sent, sizeof(status)) != 0) {
        send_status(status);
    }

    uint8_t extra[sizeof(extra_sent)];
    read_extra(extra);
    if (memcmp(extra, extra_sent, sizeof(extra)) != 0) {
        send_extra(extra);
    }

    uint8_t rgb[sizeof(rgb_sent)];
    read_rgb(rgb);
    if (memcmp(rgb, rgb_sent, sizeof(rgb)) != 0) {
        send_rgb(rgb);
    }

    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        const matrix_row_t state = matrix_get_row(row);
        if (state != keys_sent[row]) {
            send_keys(row, state);
        }
    }
}

void lcdfw_set_power(bool on) {
    if (on == powered) {
        return;
    }
    powered = on;
    if (!on) {
        waking = false;
        send_u8(UI_POWER, false);
    } else if (enabled && state_sent) {
        /* Otherwise the firmware is still loading, and gets the power state along with the rest */
        waking        = true;
        wake_attempts = 0;
        wake_timer    = timer_read32();
        send(UI_WAKE, (const uint8_t *)"", 0);
    }
}

bool lcdfw_asleep(void) {
    return !powered || waking;
}

void lcdfw_set_dim(bool on) {
    dimmed = on;
}

void lcdfw_on_keypress(uint8_t row, uint8_t col) {
    const uint8_t payload[2] = {row, col};
    send(UI_KEY_TAP, payload, sizeof(payload));
}

void lcdfw_toggle_backlight(void) {
    backlight = !backlight;
    send_u8(UI_BACKLIGHT, backlight);
}

uint8_t lcdfw_next_mode(void) {
    mode = (mode + 1) % MODE_COUNT;
    send_u8(UI_MODE, mode);
    return mode;
}

bool lcdfw_page_setting(uint8_t *saved_accent, uint8_t *saved_units, uint8_t *saved_clock_face, uint8_t *saved_media) {
    switch (mode) {
        case MODE_CAT:
        case MODE_GRAPH:
        case MODE_INFO:
            accent        = (accent + 1) % ACCENT_COUNT;
            *saved_accent = accent;
            send_u8(UI_ACCENT, accent);
            return true;
        case MODE_CLOCK:
            clock_face        = (clock_face + 1) % CLOCK_FACE_COUNT;
            *saved_clock_face = clock_face;
            send_u8(UI_CLOCK_FACE, clock_face);
            return true;
        case MODE_MEDIA:
            media        = (media + 1) % MEDIA_SETTING_COUNT;
            *saved_media = media;
            send_u8(UI_MEDIA, media);
            return true;
        case MODE_SYSTEM:
            units ^= UNITS_SYSTEM_F;
            break;
        case MODE_WEATHER:
            units ^= UNITS_WEATHER_F;
            break;
        default:
            return false;
    }
    *saved_units = units;
    send_u8(UI_UNITS, units);
    return true;
}

uint8_t lcdfw_prev_mode(void) {
    mode = (mode + MODE_COUNT - 1) % MODE_COUNT;
    send_u8(UI_MODE, mode);
    return mode;
}
