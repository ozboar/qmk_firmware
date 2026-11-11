// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "zoom65_v3.h"
#include "sleep.h"
#include "wireless.h"
#include "screen/screen.h"
#include "idle.h"

#define IDLE_DIM_MS 60000
#define IDLE_TIMEOUT_MS 180000
#define STOP_DELAY_MS 100

#define EXTI_ALL_LINES 0x7FFFF
#define NVIC_REGISTERS 8

extern void __early_init(void);
extern void last_matrix_activity_trigger(void);

/* Vendor machine code that switches the regulator around stop mode, the same as stock firmware */
static const uint32_t stop_enter_code[] = {553863175u, 554459777u, 1208378049u, 4026624001u, 688390415u, 554227969u, 3204472833u, 1198571264u, 1073807360u, 1073808388u};
static const uint32_t stop_exit_code[]  = {553863177u, 554459777u, 1208509121u, 51443856u, 4026550535u, 1745485839u, 3489677954u, 536895496u, 673389632u, 1198578684u, 1073807360u, 536866816u, 1073808388u};

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

static void stop_mcu(void) {
    chSysDisable();
    idle_arm_wake_pins();

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

    __early_init();
    rccEnableEXTI();
    rccEnableTIM2();
    rccEnableUART1();
    rccEnableUART3();
    rccEnableQSPI();
    rccEnableTIM3();
    rccEnableDMAC1();

    idle_release_wake_pins();
    chSysEnable();
}

void sleep_task(void) {
    /* Sleeping saves nothing on cable power */
    if (wireless_device() == WL_DEV_USB || kb_cable_connected()) {
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
    } else if (off_for >= STOP_DELAY_MS && !wireless_busy() && !ws2812_busy()) {
        stop_mcu();
        wireless_on_wake();
        set_lights(true);
        last_matrix_activity_trigger();
    }
}
