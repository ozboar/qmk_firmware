#include "quantum.h"
#include "lcdfw_load.h"

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

#define BL_ACK 0x79
#define BL_NACK 0x1F
#define BL_SYNC_BYTE 0x7F
#define BL_CMD_GO 0x21
#define BL_CMD_WRITE_MEMORY 0x31
#define BL_WRITE_MAX 64
#define BL_ACK_TIMEOUT_MS 100
#define BL_SYNC_TIMEOUT_MS 50
#define STAGE_FRAME_MAGIC 0xC5
#define STAGE_OP_LOAD 'L'
#define STAGE_OP_GO 'G'

#define CRC32_INITIAL 0xFFFFFFFFu
#define CRC32_POLYNOMIAL 0xEDB88320u

static bool reset_held;
static bool uart_session;

static SerialConfig uart_config = {
    STOCK_BAUD, UART_WRDLEN, UART_STPBIT, UART_PARITY, UART_ATFLCT,
};

bool lcdfw_load_owns_uart(void) {
    return reset_held || uart_session;
}

static void set_reset(bool asserted) {
    if (asserted) {
        gpio_set_pin_output_open_drain(SCREEN_NRST_PIN);
        gpio_write_pin_low(SCREEN_NRST_PIN);
    } else {
        gpio_set_pin_input(SCREEN_NRST_PIN);
    }
    reset_held = asserted;
}

static void set_uart(uint32_t baud, bool even_parity, bool session) {
    uart_config.speed       = baud;
    uart_config.UART_Parity = even_parity ? UART_Parity_Even : UART_PARITY;
    sdStop(&SD3);
    sdStart(&SD3, &uart_config);
    uart_session = session;
}

static void uart_flush_input(void) {
    while (sdGetTimeout(&SD3, TIME_IMMEDIATE) != MSG_TIMEOUT) {
    }
}

static bool wait_ack(void) {
    return sdGetTimeout(&SD3, TIME_MS2I(BL_ACK_TIMEOUT_MS)) == BL_ACK;
}

static void send_command(uint8_t command) {
    const uint8_t frame[2] = {command, (uint8_t)~command};
    sdWrite(&SD3, frame, sizeof(frame));
}

static void send_address(uint32_t address) {
    const uint8_t frame[5] = {address >> 24, (address >> 16) & 0xFF, (address >> 8) & 0xFF, address & 0xFF, (address >> 24) ^ ((address >> 16) & 0xFF) ^ ((address >> 8) & 0xFF) ^ (address & 0xFF)};
    sdWrite(&SD3, frame, sizeof(frame));
}

/* Stock firmware flags a backup register and resets into its ROM bootloader, with no reply */
static void enter_bootloader(void) {
    static const uint8_t frame[3] = {0xA5, 0x00, 0xFE};
    sdWrite(&SD3, frame, sizeof(frame));
}

/* A bootloader that has already synced answers with a NACK */
static bool bl_sync(void) {
    const uint8_t sync = BL_SYNC_BYTE;

    uart_flush_input();
    sdWrite(&SD3, &sync, 1);
    const msg_t reply = sdGetTimeout(&SD3, TIME_MS2I(BL_SYNC_TIMEOUT_MS));
    return reply == BL_ACK || reply == BL_NACK;
}

static bool bl_write(uint32_t address, const uint8_t *data, uint8_t length) {
    uint8_t count    = length - 1;
    uint8_t checksum = count;

    uart_flush_input();
    send_command(BL_CMD_WRITE_MEMORY);
    if (!wait_ack()) {
        return false;
    }
    send_address(address);
    if (!wait_ack()) {
        return false;
    }
    for (uint8_t i = 0; i < length; i++) {
        checksum ^= data[i];
    }
    sdWrite(&SD3, &count, 1);
    sdWrite(&SD3, data, length);
    sdWrite(&SD3, &checksum, 1);
    return wait_ack();
}

static bool bl_go(uint32_t address) {
    uart_flush_input();
    send_command(BL_CMD_GO);
    if (!wait_ack()) {
        return false;
    }
    send_address(address);
    return wait_ack();
}

/* Never blocks */
static bool poll_ready(uint8_t ready_byte) {
    return sdGetTimeout(&SD3, TIME_IMMEDIATE) == ready_byte;
}

static void stage_frame(uint8_t op, uint32_t address, uint16_t length) {
    const uint8_t frame[8] = {STAGE_FRAME_MAGIC, op, address & 0xFF, (address >> 8) & 0xFF, (address >> 16) & 0xFF, address >> 24, length & 0xFF, length >> 8};
    sdWrite(&SD3, frame, sizeof(frame));
}

static bool stage_begin(uint32_t address, uint16_t length) {
    uart_flush_input();
    stage_frame(STAGE_OP_LOAD, address, length);
    return true;
}

static void stage_data(const uint8_t *data, uint16_t length) {
    sdWrite(&SD3, data, length);
}

static bool stage_crc(uint32_t *crc) {
    uint8_t reply[4];

    if (sdReadTimeout(&SD3, reply, sizeof(reply), TIME_MS2I(BL_ACK_TIMEOUT_MS)) != sizeof(reply)) {
        return false;
    }
    *crc = ((uint32_t)reply[3] << 24) | ((uint32_t)reply[2] << 16) | ((uint32_t)reply[1] << 8) | reply[0];
    return true;
}

static bool stage_go(uint32_t address) {
    stage_frame(STAGE_OP_GO, address, 0);
    return true;
}

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
static bool        loaded;

static uint32_t image_crc;

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint16_t length) {
    while (length--) {
        crc ^= *data++;
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ ((crc & 1) ? CRC32_POLYNOMIAL : 0);
        }
    }
    return crc;
}

static void next_step(load_step_t next) {
    step       = next;
    step_timer = timer_read32();
}

static bool waited(uint32_t ms) {
    return timer_elapsed32(step_timer) >= ms;
}

/* Back to stock firmware, then either try again or give up and leave stock running */
static void load_failed(void) {
    set_uart(STOCK_BAUD, false, false);
    set_reset(true);
    wait_ms(RESET_PULSE_MS);
    set_reset(false);
    load_attempts++;
    next_step(load_attempts < LOAD_ATTEMPTS ? STEP_START : STEP_DONE);
}

void lcdfw_load_restart(void) {
    load_attempts = 0;
    loaded        = false;
    next_step(STEP_START);
}

void lcdfw_load_stop(void) {
    set_uart(STOCK_BAUD, false, false);
    set_reset(true);
    wait_ms(RESET_PULSE_MS);
    set_reset(false);
    loaded = false;
    next_step(STEP_DONE);
}

bool lcdfw_load_done(void) {
    return loaded;
}

bool lcdfw_load_busy(void) {
    return step != STEP_DONE;
}

bool lcdfw_load_task(void) {
    switch (step) {
        case STEP_START:
            /* Resetting first gets back to stock whatever was running */
            tries  = 0;
            offset = 0;
            set_uart(STOCK_BAUD, false, true);
            set_reset(true);
            next_step(STEP_RESET);
            break;

        case STEP_RESET:
            if (waited(RESET_PULSE_MS)) {
                set_reset(false);
                next_step(STEP_WAIT_STOCK);
            }
            break;

        case STEP_WAIT_STOCK:
            if (waited(STOCK_BOOT_MS + (uint32_t)load_attempts * STOCK_BOOT_RETRY_MS)) {
                enter_bootloader();
                next_step(STEP_WAIT_COMMAND_SENT);
            }
            break;

        case STEP_WAIT_COMMAND_SENT:
            /* Changing the UART settings would drop the command if it was still sending */
            if (waited(COMMAND_SEND_MS)) {
                set_uart(ROM_BAUD, true, true);
                next_step(STEP_WAIT_SYNC);
            }
            break;

        case STEP_WAIT_SYNC:
            if (waited(SYNC_DELAY_MS)) {
                next_step(STEP_SYNC);
            }
            break;

        case STEP_SYNC:
            if (bl_sync()) {
                next_step(STEP_WRITE_STAGE1);
            } else if (++tries >= SYNC_ATTEMPTS) {
                load_failed();
            }
            break;

        case STEP_WRITE_STAGE1: {
            /* The bootloader checks each chunk against its checksum before acking */
            uint32_t remaining = lcd_stage1_size - offset;
            uint8_t  length    = remaining < BL_WRITE_MAX ? remaining : BL_WRITE_MAX;
            if (!bl_write(STAGE1_ADDRESS + offset, lcd_stage1 + offset, length)) {
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
            if (!bl_go(STAGE1_ADDRESS)) {
                load_failed();
                break;
            }
            set_uart(STAGE1_BAUD, false, true);
            next_step(STEP_WAIT_STAGE1);
            break;

        case STEP_WAIT_STAGE1:
            if (poll_ready(STAGE1_READY)) {
                offset    = 0;
                image_crc = CRC32_INITIAL;
                if (stage_begin(LOAD_ADDRESS, lcd_image_size)) {
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
            stage_data(lcd_image + offset, length);
            image_crc = crc32_update(image_crc, lcd_image + offset, length);
            offset += length;
            if (offset >= lcd_image_size) {
                next_step(STEP_CHECK_IMAGE);
            }
            break;
        }

        case STEP_CHECK_IMAGE: {
            uint32_t crc = 0;
            if (stage_crc(&crc) && crc == ~image_crc) {
                next_step(STEP_GO_IMAGE);
            } else {
                load_failed();
            }
            break;
        }

        case STEP_GO_IMAGE:
            if (stage_go(LOAD_ADDRESS)) {
                next_step(STEP_FLUSH_GO);
            } else {
                load_failed();
            }
            break;

        case STEP_FLUSH_GO:
            /* Changing the UART settings would drop the go request if it was still sending */
            if (waited(STAGE1_FLUSH_MS)) {
                set_uart(APP_BAUD, false, true);
                next_step(STEP_WAIT_APP);
            }
            break;

        case STEP_WAIT_APP:
            if (poll_ready(APP_READY)) {
                /* Hands UART3 back. The firmware buffers frames, so the UI state can follow at once */
                set_uart(APP_BAUD, false, false);
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
