// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "zoom65_v3.h"
#include "sleep.h"
#include "wireless.h"
#include "screen/screen.h"

#define IDLE_DIM_MS 60000
#define IDLE_TIMEOUT_MS 180000
#define STOP_DELAY_MS 100

#define EXTI_ALL_LINES 0x7FFFF
#define EXTI_IRQ_PRIORITY 6
#define NVIC_REGISTERS 8

extern void __early_init(void);
extern void matrix_init_pins(void);
extern void last_matrix_activity_trigger(void);

/* Vendor machine code that switches the regulator around stop mode, the same as stock firmware */
static const uint32_t stop_enter_code[] = {553863175u, 554459777u, 1208378049u, 4026624001u, 688390415u, 554227969u, 3204472833u, 1198571264u, 1073807360u, 1073808388u};
static const uint32_t stop_exit_code[]  = {553863177u, 554459777u, 1208509121u, 51443856u, 4026550535u, 1745485839u, 3489677954u, 536895496u, 673389632u, 1198578684u, 1073807360u, 536866816u, 1073808388u};

static const pin_t col_pins[] = MATRIX_COL_PINS;
static const pin_t row_pins[] = MATRIX_ROW_PINS;

static bool     lights_off;
static bool     dimmed;
static bool     rgb_was_on;
static uint32_t lights_off_timer;

bool sleep_leds_off(void) {
    return lights_off || (dimmed && !wireless_link_active());
}

static void set_dimmed(bool on) {
    if (dimmed != on) {
        dimmed = on;
        screen_set_dim(on);
    }
}

static void run_code(const uint32_t *code) {
    ((void (*)(void))((uint32_t)code | 1))();
}

/* The screen goes off here, well before stop mode, as stopping would cut off its frame in the UART */
static void set_lights(bool on) {
    if (on) {
        set_dimmed(false);
    }
    screen_set_power(on);
    lights_off = !on;
    if (on) {
        if (rgb_was_on) {
            rgb_matrix_enable_noeeprom();
        }
    } else {
        rgb_was_on = rgb_matrix_is_enabled();
        rgb_matrix_disable_noeeprom();
    }
    kb_led_update();
}

static void prepare_wake_pins(void) {
    rccEnableEXTI();

    for (uint8_t i = 0; i < ARRAY_SIZE(col_pins); i++) {
        gpio_set_pin_output_open_drain(col_pins[i]);
        gpio_write_pin_low(col_pins[i]);
    }

    for (uint8_t i = 0; i < ARRAY_SIZE(row_pins); i++) {
        const pin_t    row  = row_pins[i];
        const uint32_t pad  = PAL_PAD(row);
        const uint32_t bit  = 1u << pad;
        const uint32_t port = ((uint32_t)PAL_PORT(row) & 0x3FFF) >> 10;

        gpio_set_pin_input_high(row);
        AFIO->EXTICR[pad >> 2] = (AFIO->EXTICR[pad >> 2] & ~(0xFu << ((pad & 3) * 4))) | (port << ((pad & 3) * 4));
        /* Only a press wakes it, so a release does not cost another idle timeout */
        EXTI->RTSR &= ~bit;
        EXTI->FTSR |= bit;
        EXTI->PR = bit;
        EXTI->IMR |= bit;
        EXTI->EMR &= ~bit;
    }

    nvicEnableVector(EXTI9_5_IRQn, EXTI_IRQ_PRIORITY);
    nvicEnableVector(EXTI15_10_IRQn, EXTI_IRQ_PRIORITY);
}

static void release_wake_pins(void) {
    for (uint8_t i = 0; i < ARRAY_SIZE(row_pins); i++) {
        EXTI->IMR &= ~(1u << PAL_PAD(row_pins[i]));
    }
    EXTI->PR = EXTI_ALL_LINES;
    nvicDisableVector(EXTI9_5_IRQn);
    nvicDisableVector(EXTI15_10_IRQn);
}

static void stop_mcu(void) {
    chSysDisable();
    prepare_wake_pins();

    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;

    /* The chip will not stop with anything pending */
    EXTI->PR = EXTI_ALL_LINES;
    for (uint8_t i = 0; i < NVIC_REGISTERS; i++) {
        NVIC->ICPR[i] = NVIC->ISPR[i];
    }
    SCB->ICSR |= SCB_ICSR_PENDSTCLR_Msk;

    PWR->CR0 &= 0x09U;
    PWR->CR0 |= 0x3B004U;
    PWR->CFGR = 0x3B3;

    run_code(stop_enter_code);
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    __WFI();
    run_code(stop_exit_code);
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;

    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;

    __early_init();
    rccEnableEXTI();
    rccEnableUART1();
    rccEnableUART3();
    rccEnableQSPI();

    release_wake_pins();
    matrix_init_pins();
    chSysEnable();
}

void sleep_task(void) {
    if (wireless_device() == WL_DEV_USB) {
        if (lights_off) {
            set_lights(true);
        }
        set_dimmed(false);
        return;
    }

    if (!lights_off) {
        set_dimmed(last_input_activity_elapsed() >= IDLE_DIM_MS);
        if ((wireless_should_sleep() || last_input_activity_elapsed() >= IDLE_TIMEOUT_MS) && !wireless_busy()) {
            set_lights(false);
            lights_off_timer = timer_read32();
        }
        return;
    }

    const uint32_t off_for = timer_elapsed32(lights_off_timer);
    if (last_input_activity_elapsed() < off_for) {
        wireless_on_wake();
        set_lights(true);
    } else if (off_for >= STOP_DELAY_MS && !wireless_busy()) {
        stop_mcu();
        wireless_on_wake();
        set_lights(true);
        last_matrix_activity_trigger();
    }
}
