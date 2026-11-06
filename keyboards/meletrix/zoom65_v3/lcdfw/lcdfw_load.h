#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Loads the screen firmware into the screen's SRAM through its ROM bootloader,
 * one small step per call so typing keeps working. Returns true while busy.
 */
bool lcdfw_load_task(void);

/* Starts a new load */
void lcdfw_load_restart(void);

/* Abandons any load and resets the screen to stock */
void lcdfw_load_stop(void);

/* True once the screen firmware is running */
bool lcdfw_load_done(void);

/* True while loading has UART3 or the screen's reset line */
bool lcdfw_load_owns_uart(void);

extern const uint8_t  lcd_image[];
extern const uint32_t lcd_image_size;
extern const uint8_t  lcd_stage1[];
extern const uint32_t lcd_stage1_size;
