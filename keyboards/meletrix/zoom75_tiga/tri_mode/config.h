// Copyright 2024 MHooijberg <https://github.com/MHooijberg>
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// External clock configuration
#undef AT32_HEXTCLK
#define AT32_HEXTCLK 16000000

// Encoder configuration
#undef ENCODER_MAP_KEY_DELAY
#define ENCODER_MAP_KEY_DELAY 10

// WS2812 PWM Driver
#define WS2812_PWM_DRIVER PWMD5
#define WS2812_PWM_CHANNEL 2
#define WS2812_PWM_DMA_STREAM AT32_DMA1_STREAM2
#define WS2812_PWM_DMAMUX_CHANNEL 2
#define WS2812_PWM_DMAMUX_ID AT32_DMAMUX_TMR5_OVERFLOW

// RGB LED configuration
#define WS2812_TIMING 1250
#define WS2812_T1H 850
#define WS2812_T0H 250