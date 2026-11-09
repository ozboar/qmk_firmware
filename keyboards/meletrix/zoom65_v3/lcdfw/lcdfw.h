#pragma once

#include <stdbool.h>
#include <stdint.h>

void    lcdfw_init(uint8_t mode, uint8_t accent, uint8_t units, uint8_t clock_face, uint8_t media);
void    lcdfw_task(void);

/* Loads the firmware onto the screen, or resets the screen back to stock */
void    lcdfw_start(void);
void    lcdfw_stop(void);
void    lcdfw_set_power(bool on);

/* True while the screen is off or still waking, when it takes no frames */
bool    lcdfw_asleep(void);

/* Lowers the backlight for idle */
void    lcdfw_set_dim(bool on);
void    lcdfw_on_keypress(uint8_t row, uint8_t col);
void    lcdfw_toggle_backlight(void);

/* Shows the RGB page briefly, then the screen returns to the current mode */
void    lcdfw_show_rgb(void);
uint8_t lcdfw_next_mode(void);
uint8_t lcdfw_prev_mode(void);

/* Changes the current page's setting. Returns true if a setting changed and needs saving */
bool    lcdfw_page_setting(uint8_t *accent, uint8_t *units, uint8_t *clock_face, uint8_t *media);
