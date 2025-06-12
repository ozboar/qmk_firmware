#pragma once
#include_next <board.h>

// Undefine the MCU family variant.
#undef AT32F415KB

// Redefine the correct MCU family variant.
#define AT32F415RC
