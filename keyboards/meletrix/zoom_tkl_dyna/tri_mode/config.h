// Copyright 2023-2025 HorrorTroll <https://github.com/HorrorTroll>
// Copyright 2023-2025 Zhaqian <https://github.com/zhaqian12>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Encoder configuration
// #undef ENCODER_MAP_KEY_DELAY
// #define ENCODER_MAP_KEY_DELAY 10

// WS2812 PWM Driver
#undef WS2812_PWM_PAL_MODE
#undef WS2812_PWM_DRIVER
#undef WS2812_PWM_CHANNEL
#undef WS2812_PWM_DMA_STREAM
#undef WS2812_PWM_DMAMUX_ID  
#undef WS2812_DI_PIN

#define WS2812_PWM_DRIVER PWMD4
#define WS2812_PWM_CHANNEL 3
#define WS2812_PWM_DMA_STREAM AT32_DMA1_STREAM2
#define WS2812_PWM_DMAMUX_ID  AT32_DMAMUX_TMR4_OVERFLOW
#define WS2812_PWM_PAL_MODE 2
#define WS2812_DI_PIN B8
#define WS2812_EXTERNAL_PULLUP

// RGB LED configuration
#define WS2812_TIMING 1250
#define WS2812_T1H 850
#define WS2812_T0H 250
