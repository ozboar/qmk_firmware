// Copyright 2024 MHooijberg <https://github.com/MHooijberg>
// SPDX-License-Identifier: GPL-2.0-or-later

// The following settings are needed to configure,
// the 16MHz external clock.

#pragma once
#include_next <mcuconf.h>

// Undefine the default settings.
#undef AT32_ADCDIV
#undef AT32_AHBDIV
#undef AT32_APB1DIV
#undef AT32_APB2DIV
#undef AT32_HEXT_ENABLED
#undef AT32_PLL_FR_VALUE
#undef AT32_PLL_MS_VALUE
#undef AT32_PLL_NS_VALUE
#undef AT32_PLLHEXTDIV
#undef AT32_PLLMULT_VALUE
#undef AT32_PLLRCS
#undef AT32_PVM_ENABLE
#undef AT32_USBDIV

// Define new settings.
#define AT32_ADCDIV                         AT32_ADCDIV_DIV4
#define AT32_AHBDIV                         AT32_AHBDIV_DIV1
#define AT32_APB1DIV                        AT32_APB1DIV_DIV2
#define AT32_APB2DIV                        AT32_APB2DIV_DIV2
#define AT32_HEXT_ENABLED                   TRUE
#define AT32_PLL_FR_VALUE                   4
#define AT32_PLL_MS_VALUE                   1
#define AT32_PLL_NS_VALUE                   72
#define AT32_PLLHEXTDIV                     AT32_PLLHEXTDIV_DIV2
#define AT32_PLLMULT_VALUE                  18
#define AT32_PLLRCS                         AT32_PLLRCS_HEXT
#define AT32_PVM_ENABLE                     FALSE
#define AT32_USBDIV                         AT32_USBDIV_DIV3

// Setup definitions for the SPI driver, for the external NOR Flash.
// #undef AT32_SPI_USE_SPI2
// #define STM32_SPI_USE_SPI2 TRUE
// #define SPI_SELECT_MODE SPI_SELECT_MODE_NONE
