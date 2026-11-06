#include "quantum.h"
#include "lcdfw_load.h"
#include "screen_probe/screen_probe.h"

#define LOAD_ADDRESS 0x20008000u
#define STAGE1_ADDRESS 0x20016000u
#define STOCK_BAUD 115200
#define ROM_BAUD 230400
#define STAGE1_BAUD 3000000
#define APP_BAUD 1500000
#define STAGE1_READY 0x5B
#define APP_READY 0x5A
#define STAGE1_CHUNK 256

#define RESET_PULSE_MS 20
/* Stock firmware has to be up to take the command that enters its bootloader. Each retry waits longer */
#define STOCK_BOOT_MS 100
#define STOCK_BOOT_RETRY_MS 200
#define COMMAND_SEND_MS 5
/* Syncing too early or too often gets no answer. Each attempt waits up to 50 ms for a reply */
#define SYNC_DELAY_MS 100
#define SYNC_ATTEMPTS 6
#define STAGE1_FLUSH_MS 2
#define STAGE1_READY_TIMEOUT_MS 100
#define APP_READY_TIMEOUT_MS 200
#define LOAD_ATTEMPTS 3

typedef enum {
    STEP_START,
    STEP_RESET,
    STEP_WAIT_STOCK,
    STEP_WAIT_COMMAND_SENT,
    STEP_WAIT_SYNC,
    STEP_SYNC,
    STEP_WRITE_STAGE1,
    STEP_GO_STAGE1,
    STEP_WAIT_STAGE1,
    STEP_SEND_IMAGE,
    STEP_CHECK_IMAGE,
    STEP_GO_IMAGE,
    STEP_FLUSH_GO,
    STEP_WAIT_APP,
    STEP_DONE,
} load_step_t;

static load_step_t step = STEP_START;
static uint32_t    step_timer;
static uint32_t    offset;
static uint8_t     tries;
static uint8_t     load_attempts;
static uint32_t    load_start;
static uint16_t    load_duration;
static bool        loaded;
static uint8_t     failed_step;

static void next_step(load_step_t next) {
    step       = next;
    step_timer = timer_read32();
}

static bool waited(uint32_t ms) {
    return timer_elapsed32(step_timer) >= ms;
}

/* Back to stock firmware, then either try again or give up and leave stock running */
static void load_failed(void) {
    failed_step = step;
    screen_bl_uart(STOCK_BAUD, false, false);
    screen_bl_reset(true);
    wait_ms(RESET_PULSE_MS);
    screen_bl_reset(false);
    load_attempts++;
    load_duration = timer_elapsed32(load_start);
    next_step(load_attempts < LOAD_ATTEMPTS ? STEP_START : STEP_DONE);
}

void lcdfw_load_restart(void) {
    load_attempts = 0;
    failed_step   = 0;
    loaded        = false;
    next_step(STEP_START);
}

void lcdfw_load_stop(void) {
    screen_bl_uart(STOCK_BAUD, false, false);
    screen_bl_reset(true);
    wait_ms(RESET_PULSE_MS);
    screen_bl_reset(false);
    loaded = false;
    next_step(STEP_DONE);
}

bool lcdfw_load_done(void) {
    return loaded;
}

void lcdfw_load_report(uint8_t *out) {
    out[0] = loaded;
    out[1] = load_attempts + (loaded ? 1 : 0);
    out[2] = load_duration >> 8;
    out[3] = load_duration & 0xFF;
    out[4] = failed_step;
}

bool lcdfw_load_task(void) {
    switch (step) {
        case STEP_START:
            /* Resetting first gets back to stock whatever was running */
            tries  = 0;
            offset = 0;
            if (load_attempts == 0) {
                load_start = timer_read32();
            }
            screen_bl_uart(STOCK_BAUD, false, true);
            screen_bl_reset(true);
            next_step(STEP_RESET);
            break;

        case STEP_RESET:
            if (waited(RESET_PULSE_MS)) {
                screen_bl_reset(false);
                next_step(STEP_WAIT_STOCK);
            }
            break;

        case STEP_WAIT_STOCK:
            if (waited(STOCK_BOOT_MS + (uint32_t)load_attempts * STOCK_BOOT_RETRY_MS)) {
                screen_bl_enter_from_stock();
                next_step(STEP_WAIT_COMMAND_SENT);
            }
            break;

        case STEP_WAIT_COMMAND_SENT:
            /* Changing the UART settings would drop the command if it was still sending */
            if (waited(COMMAND_SEND_MS)) {
                screen_bl_uart(ROM_BAUD, true, true);
                next_step(STEP_WAIT_SYNC);
            }
            break;

        case STEP_WAIT_SYNC:
            if (waited(SYNC_DELAY_MS)) {
                next_step(STEP_SYNC);
            }
            break;

        case STEP_SYNC:
            if (screen_bl_sync()) {
                next_step(STEP_WRITE_STAGE1);
            } else if (++tries >= SYNC_ATTEMPTS) {
                load_failed();
            }
            break;

        case STEP_WRITE_STAGE1: {
            /* The bootloader checks each chunk against its checksum before acking */
            uint32_t remaining = lcd_stage1_size - offset;
            uint8_t  length    = remaining < SCREEN_BL_WRITE_MAX ? remaining : SCREEN_BL_WRITE_MAX;
            if (!screen_bl_write(STAGE1_ADDRESS + offset, lcd_stage1 + offset, length)) {
                load_failed();
                break;
            }
            offset += length;
            if (offset >= lcd_stage1_size) {
                next_step(STEP_GO_STAGE1);
            }
            break;
        }

        case STEP_GO_STAGE1:
            if (!screen_bl_go(STAGE1_ADDRESS)) {
                load_failed();
                break;
            }
            screen_bl_uart(STAGE1_BAUD, false, true);
            next_step(STEP_WAIT_STAGE1);
            break;

        case STEP_WAIT_STAGE1:
            if (screen_bl_poll_ready(STAGE1_READY)) {
                offset = 0;
                if (screen_stage_begin(LOAD_ADDRESS, lcd_image_size)) {
                    next_step(STEP_SEND_IMAGE);
                } else {
                    load_failed();
                }
            } else if (waited(STAGE1_READY_TIMEOUT_MS)) {
                load_failed();
            }
            break;

        case STEP_SEND_IMAGE: {
            uint32_t remaining = lcd_image_size - offset;
            uint16_t length    = remaining < STAGE1_CHUNK ? remaining : STAGE1_CHUNK;
            screen_stage_data(lcd_image + offset, length);
            offset += length;
            if (offset >= lcd_image_size) {
                next_step(STEP_CHECK_IMAGE);
            }
            break;
        }

        case STEP_CHECK_IMAGE: {
            uint32_t crc = 0;
            if (screen_stage_crc(&crc) && crc == lcd_image_crc) {
                next_step(STEP_GO_IMAGE);
            } else {
                load_failed();
            }
            break;
        }

        case STEP_GO_IMAGE:
            if (screen_stage_go(LOAD_ADDRESS)) {
                next_step(STEP_FLUSH_GO);
            } else {
                load_failed();
            }
            break;

        case STEP_FLUSH_GO:
            /* Changing the UART settings would drop the go request if it was still sending */
            if (waited(STAGE1_FLUSH_MS)) {
                screen_bl_uart(APP_BAUD, false, true);
                next_step(STEP_WAIT_APP);
            }
            break;

        case STEP_WAIT_APP:
            if (screen_bl_poll_ready(APP_READY)) {
                /* Hands UART3 back. The firmware buffers frames, so the UI state can follow at once */
                screen_bl_uart(APP_BAUD, false, false);
                load_duration = timer_elapsed32(load_start);
                loaded        = true;
                next_step(STEP_DONE);
            } else if (waited(APP_READY_TIMEOUT_MS)) {
                load_failed();
            }
            break;

        case STEP_DONE:
            return false;
    }
    return true;
}
