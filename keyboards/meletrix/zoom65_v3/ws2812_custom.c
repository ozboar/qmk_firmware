// Copyright 2026 emolitor (github.com/emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * WS2812 output by timer triggered DMA to the pin's BSRR, with no interrupt lockout. Adapted from
 * keyboards/shortcut/bridge75/ws2812_custom.c in emolitor's wireless-3.0 branch, which found the
 * chip's limits: a DMA block is at most 511 transfers, and a PWM output would glitch between
 * blocks, while a GPIO just stays low. This version encodes blocks in the DMA interrupt, so a
 * flush returns straight away instead of holding up the matrix.
 */
#include "quantum.h"
#include "ws2812.h"
#include "hal.h"
#include "zoom65_v3.h"

#define PIN_SET (1U << PAL_PAD(WS2812_DI_PIN))
#define PIN_RESET (1U << (PAL_PAD(WS2812_DI_PIN) + 16))

/* Three phases to a bit: high, then low for a 0, then low for a 1 */
#define PHASES_PER_BIT 3
#define PHASES_PER_LED (WS2812_CHANNELS * 8 * PHASES_PER_BIT)
#define PHASE_NS 385U
#define PHASE_TICKS ((CPU_CLOCK / 1000000U * PHASE_NS + 500U) / 1000U)

#define WS2812_CHANNELS 3
#define LEDS_PER_BLOCK 7
/* The first transfers of a block run late, which would stretch a first high phase into a 1 bit */
#define LEAD_IN_PHASES 3
#define BLOCK_PHASES (LEAD_IN_PHASES + LEDS_PER_BLOCK * PHASES_PER_LED)
#define BLOCK_COUNT ((WS2812_LED_COUNT + LEDS_PER_BLOCK - 1) / LEDS_PER_BLOCK)

#define DMA_STREAM WB32_DMA1_STREAM1
#define DMA_IRQ_PRIORITY 3
#define TRANSFER_TIMEOUT_MS 10

_Static_assert(BLOCK_PHASES <= 511, "over the DMA block limit");

ws2812_led_t ws2812_leds[WS2812_LED_COUNT];

static uint32_t                 buffers[2][BLOCK_PHASES];
static const wb32_dma_stream_t *stream;
static volatile uint8_t         current_block;
static volatile bool            active;
static uint32_t                 transfer_timer;
static uint32_t                 flushes;

static const GPTConfig gpt_config = {
    .frequency = CPU_CLOCK,
    .callback  = NULL,
    .cr2       = 0,
    .dier      = WB32_TIM_DIER_UDE,
};

static uint8_t block_leds(uint8_t block) {
    const uint16_t remaining = WS2812_LED_COUNT - block * LEDS_PER_BLOCK;
    return remaining > LEDS_PER_BLOCK ? LEDS_PER_BLOCK : remaining;
}

/* The struct's byte layout already follows the LEDs' color order */
static void fill_block(uint8_t block) {
    uint32_t      *out = buffers[block % 2];
    const uint8_t *in  = (const uint8_t *)&ws2812_leds[block * LEDS_PER_BLOCK];

    for (uint8_t i = 0; i < LEAD_IN_PHASES; i++) {
        *out++ = 0;
    }
    for (uint16_t i = block_leds(block) * WS2812_CHANNELS; i > 0; i--) {
        const uint8_t byte = *in++;
        for (int8_t bit = 7; bit >= 0; bit--) {
            const uint32_t one = (byte >> bit) & 1;
            *out++             = PIN_SET;
            *out++             = (1 - one) * PIN_RESET;
            *out++             = one * PIN_RESET;
        }
    }
}

/* A timer update left pending would fire the first transfer of the block early */
static void arm_block(uint8_t block) {
    dmaStreamSetSource(stream, buffers[block % 2]);
    dmaStreamSetTransactionSize(stream, LEAD_IN_PHASES + block_leds(block) * PHASES_PER_LED);
    GPTD3.tim->DIER &= ~WB32_TIM_DIER_UDE;
    GPTD3.tim->CNT = 0;
    /* This status register is plain read and write, so clear all of it */
    GPTD3.tim->SR = 0;
    dmaStreamEnable(stream);
    GPTD3.tim->DIER |= WB32_TIM_DIER_UDE;
}

static void finish(void) {
    dmaStreamDisable(stream);
    gptStopTimerI(&GPTD3);
    PAL_PORT(WS2812_DI_PIN)->BSRR = PIN_RESET;
    active                        = false;
}

/* The pin holds low while this runs, which the LEDs read as a longer low phase */
static void dma_callback(void *arg, uint32_t flags) {
    (void)arg;

    if (flags & WB32_DMAC_IT_STATE_ERR) {
        finish();
    } else if ((flags & WB32_DMAC_IT_STATE_TFR) && active) {
        const uint8_t next = current_block + 1;
        if (next >= BLOCK_COUNT) {
            finish();
            return;
        }
        current_block = next;
        arm_block(next);
        if (next + 1 < BLOCK_COUNT) {
            fill_block(next + 1);
        }
    }
}

void ws2812_init(void) {
    gpio_write_pin_low(WS2812_DI_PIN);
    gpio_set_pin_output(WS2812_DI_PIN);

    gptStart(&GPTD3, &gpt_config);
    stream = dmaStreamAlloc(DMA_STREAM - WB32_DMA_STREAM(0), DMA_IRQ_PRIORITY, dma_callback, NULL);
    if (stream == NULL) {
        return;
    }
    dmaStreamSetDestination(stream, &PAL_PORT(WS2812_DI_PIN)->BSRR);
    dmaStreamSetMode(stream, WB32_DMA_CHCFG_HWHIF(WB32_DMAC_HWHIF_TIM3_UP) | WB32_DMA_CHCFG_DIR_M2P | WB32_DMA_CHCFG_PSIZE_WORD | WB32_DMA_CHCFG_MSIZE_WORD | WB32_DMA_CHCFG_MINC | WB32_DMA_CHCFG_TCIE | WB32_DMA_CHCFG_TEIE | WB32_DMA_CHCFG_PL(3));
    /* The mode above leaves the memory side waiting on a hardware request that never comes */
    stream->dmac->Ch[stream->channel].CFGL |= WB32_DMAC_SRC_HIFS_SW;
}

void ws2812_set_color(int index, uint8_t red, uint8_t green, uint8_t blue) {
    ws2812_leds[index].r = red;
    ws2812_leds[index].g = green;
    ws2812_leds[index].b = blue;
}

void ws2812_set_color_all(uint8_t red, uint8_t green, uint8_t blue) {
    for (int i = 0; i < WS2812_LED_COUNT; i++) {
        ws2812_set_color(i, red, green, blue);
    }
}

bool ws2812_busy(void) {
    return active;
}

uint32_t ws2812_flushes(void) {
    return flushes;
}

/* A frame still sending is left to finish, and the next flush carries the newer colors */
void ws2812_flush(void) {
    flushes++;
    if (stream == NULL) {
        return;
    }
    if (active) {
        if (timer_elapsed32(transfer_timer) >= TRANSFER_TIMEOUT_MS) {
            chSysLock();
            finish();
            dmaStreamClearInterrupt(stream);
            chSysUnlock();
        }
        return;
    }
    if (!kb_led_powered()) {
        return;
    }

    fill_block(0);
    if (BLOCK_COUNT > 1) {
        fill_block(1);
    }
    current_block  = 0;
    active         = true;
    transfer_timer = timer_read32();

    dmaStreamEnableInterrupt(stream, WB32_DMAC_IT_TFR);
    dmaStreamEnableInterrupt(stream, WB32_DMAC_IT_ERR);
    GPTD3.tim->DIER &= ~WB32_TIM_DIER_UDE;
    gptStartContinuous(&GPTD3, PHASE_TICKS);
    chSysLock();
    arm_block(0);
    chSysUnlock();
}
