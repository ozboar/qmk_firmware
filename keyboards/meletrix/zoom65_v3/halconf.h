#pragma once

#define HAL_USE_SERIAL TRUE
#define HAL_USE_SPI TRUE
#define SPI_USE_WAIT TRUE
#define SPI_SELECT_MODE SPI_SELECT_MODE_PAD

#ifdef SCREEN_PROBE_ENABLE
#    define SERIAL_BUFFERS_SIZE 2048
#else
#    define SERIAL_BUFFERS_SIZE 128
#endif

#include_next <halconf.h>
