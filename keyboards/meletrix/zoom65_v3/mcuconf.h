#pragma once

#include_next <mcuconf.h>

#undef WB32_SPI_USE_QSPI
#define WB32_SPI_USE_QSPI TRUE

#undef WB32_SERIAL_USE_UART1
#define WB32_SERIAL_USE_UART1 TRUE

#undef WB32_SERIAL_UART1_PRIORITY
#define WB32_SERIAL_UART1_PRIORITY 8

#undef WB32_SERIAL_USE_UART3
#define WB32_SERIAL_USE_UART3 TRUE

/* 96 MHz. The WS2812 bit banging leaves unlit channels glowing at the default 72 MHz */
#undef WB32_PLLDIV_VALUE
#define WB32_PLLDIV_VALUE 2

#undef WB32_PLLMUL_VALUE
#define WB32_PLLMUL_VALUE 16

#undef WB32_USBPRE
#define WB32_USBPRE WB32_USBPRE_DIV2
