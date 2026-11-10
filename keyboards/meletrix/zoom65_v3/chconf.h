#pragma once

/* Tickless on TIM2, a 16 bit timer, so the counter wraps every 6.5 s at 10 kHz */
#define CH_CFG_ST_RESOLUTION 16
#define CH_CFG_ST_FREQUENCY 10000

#include_next <chconf.h>

#undef CH_CFG_ST_TIMEDELTA
#define CH_CFG_ST_TIMEDELTA 2
