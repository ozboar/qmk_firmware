#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Report id for probe commands on the raw HID interface */
#define SCREEN_PROBE_REPORT_ID 89

/* True while the probe owns UART3 or drives BOOT0/NRST */
bool screen_probe_active(void);

/* Bootloader steps, also used to load the screen firmware at boot */
void screen_bl_reset(bool asserted);
void screen_bl_uart(uint32_t baud, bool even_parity, bool session);
void screen_bl_enter_from_stock(void);
bool screen_bl_sync(void);
bool screen_bl_write(uint32_t address, const uint8_t *data, uint8_t length);
bool screen_bl_go(uint32_t address);

/* Never blocks. True once the expected byte arrives */
bool screen_bl_poll_ready(uint8_t ready_byte);

/* First stage loader, only available once it has answered a ping */
bool screen_stage_begin(uint32_t address, uint16_t length);
void screen_stage_data(const uint8_t *data, uint16_t length);
bool screen_stage_crc(uint32_t *crc);
bool screen_stage_go(uint32_t address);

#define SCREEN_BL_WRITE_MAX 64

/* Handle one 32 byte raw HID report and write the 32 byte reply */
void screen_probe_command(const uint8_t *data, uint8_t *reply);

typedef void (*screen_probe_send_t)(uint8_t *data, uint8_t length);

/* Send any data the last command queued, after its reply has gone out */
void screen_probe_stream(screen_probe_send_t send);
