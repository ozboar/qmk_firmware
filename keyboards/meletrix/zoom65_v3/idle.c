// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "zoom65_v3.h"
#include "idle.h"
#include "wireless/wireless.h"
#include "screen/screen.h"

/* Longer than the tapping term, so timers that run after a key goes up still fire on time */
#ifndef IDLE_GRACE_MS
#    define IDLE_GRACE_MS 300
#endif

/* The screen task runs on this interval */
#ifndef IDLE_MAX_WAIT_MS
#    define IDLE_MAX_WAIT_MS 20
#endif

#define EXTI_LINES_9_5 0x03E0u
#define EXTI_LINES_15_10 0xFC00u
#define NVIC_REGISTERS 2
#define EXTI_IRQ_PRIORITY 6

/* The system timer's interrupt only ends the wait through the deadline */
#if CH_CFG_ST_TIMEDELTA > 0
#    if WB32_ST_USE_TIMER != 2
#        error "the idle wait expects the system timer on TIM2"
#    endif
#    define ST_IRQ_MASK0 (1u << WB32_TIM2_NUMBER)
#else
#    define ST_IRQ_MASK0 0u
#endif

typedef enum {
    WAKE_NONE,
    WAKE_PIN,
    WAKE_TIMEOUT,
    WAKE_IRQ,
    WAKE_REASONS,
} wake_reason_t;

typedef enum {
    HOLD_INPUT,
    HOLD_KEYS,
    HOLD_WIRELESS,
    HOLD_SCREEN,
    HOLD_FRAME_RENDERING,
    HOLD_FRAME_DUE,
    HOLD_REASONS,
} hold_reason_t;

#ifdef IDLE_STATS_ENABLE
#    define STAT(x) x
#    define STATS_PAGE_WAIT 0
#    define STATS_PAGE_HOLD 1

static uint32_t stats_timer;
static uint32_t stats_cycles;
static uint32_t stats_slept_cycles;
static uint32_t stats_passes;
static uint32_t stats_scans;
static uint32_t stats_wakes[WAKE_REASONS];
static uint32_t stats_holds[HOLD_REASONS];
#else
#    define STAT(x)
#endif

extern void matrix_init_pins(void);

static const pin_t col_pins[] = MATRIX_COL_PINS;
static const pin_t row_pins[] = MATRIX_ROW_PINS;

static volatile wake_reason_t wake_reason;
static volatile bool          edge_seen;
static bool                   parked;
static uint32_t               wake_lines;
static uint32_t               pin_wake_timer;
static virtual_timer_t        deadline_timer;

static uint32_t frame_seen;
static uint32_t frame_flushes;

static void serve_exti(uint32_t lines) {
    const uint32_t pending = EXTI->PR & lines;

    EXTI->PR = pending;
    if (pending & wake_lines) {
        edge_seen   = true;
        wake_reason = WAKE_PIN;
    }
}

OSAL_IRQ_HANDLER(WB32_EXTI9_5_IRQ_VECTOR) {
    OSAL_IRQ_PROLOGUE();
    serve_exti(EXTI_LINES_9_5);
    OSAL_IRQ_EPILOGUE();
}

OSAL_IRQ_HANDLER(WB32_EXTI15_10_IRQ_VECTOR) {
    OSAL_IRQ_PROLOGUE();
    serve_exti(EXTI_LINES_15_10);
    OSAL_IRQ_EPILOGUE();
}

static void deadline_cb(virtual_timer_t *vtp, void *arg) {
    (void)vtp;
    (void)arg;
    if (wake_reason == WAKE_NONE) {
        wake_reason = WAKE_TIMEOUT;
    }
}

static void arm_wake_pin(pin_t pin, bool both_edges) {
    const uint32_t pad  = PAL_PAD(pin);
    const uint32_t bit  = 1u << pad;
    const uint32_t port = ((uint32_t)PAL_PORT(pin) & 0x3FFF) >> 10;

    gpio_set_pin_input_high(pin);
    AFIO->EXTICR[pad >> 2] = (AFIO->EXTICR[pad >> 2] & ~(0xFu << ((pad & 3) * 4))) | (port << ((pad & 3) * 4));
    if (both_edges) {
        EXTI->RTSR |= bit;
    } else {
        EXTI->RTSR &= ~bit;
    }
    EXTI->FTSR |= bit;
    EXTI->PR = bit;
    EXTI->EMR &= ~bit;
    EXTI->IMR |= bit;
    wake_lines |= bit;
}

/* Rows first, so a key already held pulls its row down once the columns drive and wakes it at once */
void idle_arm_wake_pins(void) {
    rccEnableEXTI();
    /* The WB32 port never calls irqInit, so nothing else enables these */
    nvicEnableVector(EXTI9_5_IRQn, EXTI_IRQ_PRIORITY);
    nvicEnableVector(EXTI15_10_IRQn, EXTI_IRQ_PRIORITY);
    wake_lines = 0;

    /* Only a press wakes it, so a release does not cost another wait */
    for (uint8_t i = 0; i < ARRAY_SIZE(row_pins); i++) {
        arm_wake_pin(row_pins[i], false);
    }
    /* The knob can rest at either level, so any step changes this pin one way or the other */
    arm_wake_pin(ENCODER_WAKE_PIN, true);

    for (uint8_t i = 0; i < ARRAY_SIZE(col_pins); i++) {
        gpio_set_pin_output_open_drain(col_pins[i]);
        gpio_write_pin_low(col_pins[i]);
    }
    parked = true;
}

void idle_release_wake_pins(void) {
    EXTI->IMR &= ~wake_lines;
    EXTI->PR   = wake_lines;
    wake_lines = 0;
    parked     = false;
    matrix_init_pins();
}

/*
 * With no key down the matrix stays parked between scans, columns low and rows armed, and a scan
 * is skipped until a row or the knob moves. QMK then sees the same empty matrix it saw last time.
 */
void matrix_read_rows_on_col(matrix_row_t current_matrix[], uint8_t current_col, matrix_row_t row_shifter) {
    static bool skip;

    if (current_col == 0) {
        skip = parked && !edge_seen;
        if (skip) {
            return;
        }
        STAT(stats_scans++);
        edge_seen = false;
        if (parked) {
            idle_release_wake_pins();
        }
    } else if (skip) {
        return;
    }

    const pin_t col     = col_pins[current_col];
    bool        pressed = false;

    gpio_write_pin_low(col);
    gpio_set_pin_output(col);
    matrix_output_select_delay();
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        if (!gpio_read_pin(row_pins[row])) {
            current_matrix[row] |= row_shifter;
            pressed = true;
        } else {
            current_matrix[row] &= ~row_shifter;
        }
    }
    gpio_set_pin_input_high(col);
    matrix_output_unselect_delay(current_col, pressed);

    if (current_col == MATRIX_COLS - 1) {
        for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
            if (current_matrix[row]) {
                return;
            }
        }
        idle_arm_wake_pins();
    }
}

static bool other_irq_pending(void) {
    uint32_t pending = 0;

    for (uint8_t i = 0; i < NVIC_REGISTERS; i++) {
        pending |= NVIC->ISPR[i] & NVIC->ISER[i] & (i == 0 ? ~ST_IRQ_MASK0 : ~0u);
    }
    return pending != 0;
}

static bool keys_down(void) {
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        if (matrix_get_row(row)) {
            return true;
        }
    }
    return false;
}

/* The rgb_matrix frame starts with g_rgb_timer and ends with a flush, then waits out its frame time */
static void track_frame(void) {
    if (g_rgb_timer != frame_seen) {
        frame_seen    = g_rgb_timer;
        frame_flushes = ws2812_flushes();
    }
}

static uint32_t hold(hold_reason_t reason) {
    STAT(stats_holds[reason]++);
    (void)reason;
    return 0;
}

static uint32_t wait_limit(void) {
    if (last_input_activity_elapsed() < IDLE_GRACE_MS || timer_elapsed32(pin_wake_timer) < IDLE_GRACE_MS) {
        return hold(HOLD_INPUT);
    }
    /* Parked means no key is down at the pins, and the rows are armed to end the wait */
    if (!parked || keys_down()) {
        return hold(HOLD_KEYS);
    }
    if (wireless_busy()) {
        return hold(HOLD_WIRELESS);
    }
    if (screen_busy()) {
        return hold(HOLD_SCREEN);
    }
    if (!kb_led_powered()) {
        return IDLE_MAX_WAIT_MS;
    }
    if (ws2812_flushes() == frame_flushes) {
        return hold(HOLD_FRAME_RENDERING);
    }

    const uint32_t elapsed = timer_elapsed32(g_rgb_timer);
    if (elapsed >= RGB_MATRIX_LED_FLUSH_LIMIT) {
        return hold(HOLD_FRAME_DUE);
    }
    return MIN(RGB_MATRIX_LED_FLUSH_LIMIT - elapsed, IDLE_MAX_WAIT_MS);
}

static void wait_for_wake(uint32_t ms) {
    wake_reason = WAKE_NONE;
    chVTSet(&deadline_timer, TIME_MS2I(ms), deadline_cb, NULL);

    /* Interrupts stay masked from the check to the WFI, so a wake between them is not lost */
    for (;;) {
        __disable_irq();
        if (wake_reason != WAKE_NONE) {
            __enable_irq();
            break;
        }
        /* The cycle counter keeps running through WFI, so the sleep is the difference across it */
        STAT(const uint32_t before = DWT->CYCCNT);
        __WFI();
        STAT(stats_slept_cycles += DWT->CYCCNT - before);
        const bool other = other_irq_pending();
        __enable_irq();
        if (other) {
            break;
        }
    }

    /* The pins stay parked, and the next scan reads them if an edge woke it */
    chVTReset(&deadline_timer);

    const wake_reason_t reason = wake_reason == WAKE_NONE ? WAKE_IRQ : wake_reason;
    if (reason == WAKE_PIN) {
        pin_wake_timer = timer_read32();
    }
    STAT(stats_wakes[reason]++);
}

void idle_task(void) {
    STAT(stats_passes++);
    track_frame();

    const uint32_t ms = wait_limit();
    if (ms) {
        wait_for_wake(ms);
    }
}

#ifdef IDLE_STATS_ENABLE
/* data[1] picks the page. The wait page also starts a new window for both */
void idle_stats_report(uint8_t *data, uint8_t length) {
    uint32_t values[7];

    if (length < 2 + sizeof(values)) {
        return;
    }

    if (data[1] == STATS_PAGE_HOLD) {
        _Static_assert(HOLD_REASONS < ARRAY_SIZE(values), "hold reasons do not fit the report");
        memset(values, 0, sizeof(values));
        memcpy(values, stats_holds, sizeof(stats_holds));
        values[ARRAY_SIZE(values) - 1] = stats_scans;
    } else {
        const uint32_t cycles = DWT->CYCCNT;

        values[0] = timer_elapsed32(stats_timer);
        values[1] = cycles - stats_cycles;
        values[2] = stats_slept_cycles;
        values[3] = stats_passes;
        values[4] = stats_wakes[WAKE_PIN];
        values[5] = stats_wakes[WAKE_TIMEOUT];
        values[6] = stats_wakes[WAKE_IRQ];

        stats_timer        = timer_read32();
        stats_cycles       = cycles;
        stats_slept_cycles = 0;
        stats_passes       = 0;
        stats_scans        = 0;
        memset(stats_wakes, 0, sizeof(stats_wakes));
        memset(stats_holds, 0, sizeof(stats_holds));
    }

    data[1] = ARRAY_SIZE(values);
    memcpy(&data[2], values, sizeof(values));
}
#endif
