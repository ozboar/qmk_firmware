#include QMK_KEYBOARD_H
#include <string.h>
#include "uart.h"
#include "screen_probe.h"
#ifdef LCDFW_ENABLE
#    include "lcdfw/lcdfw_load.h"
#endif

/*
 * Probe for the stock screen MCU's ROM bootloader.
 *
 * There is no raw passthrough. Write and Go only accept SRAM addresses, and the
 * remaining bootloader commands are read only, so no host command can erase or
 * write the screen MCU's flash. Requests to the SRAM program are only sent after
 * it has answered a ping, because the bootloader would treat them as commands.
 */

#define PROBE_BOOT0_PIN A8
#define PROBE_NRST_PIN B13

#define BL_ACK 0x79
#define BL_NACK 0x1F
#define BL_SYNC_BYTE 0x7F
#define BL_CMD_GET 0x00
#define BL_CMD_GET_VERSION 0x01
#define BL_CMD_GET_ID 0x02
#define BL_CMD_READ_MEMORY 0x11
#define BL_CMD_GO 0x21
#define BL_CMD_WRITE_MEMORY 0x31

#define SRAM_START 0x20000000u
#define SRAM_END 0x20040000u
#define EXT_FLASH_START 0x08400000u
#define EXT_FLASH_END 0x09400000u

#define STUB_FRAME_MAGIC 0xC5
#define STUB_OP_PING 'P'
#define STUB_OP_MAP 'M'
#define STUB_OP_READ 'R'
#define STAGE_OP_LOAD 'L'
#define STAGE_OP_GO 'G'
#define STUB_READ_MAX 1920
#define STUB_MAP_MAX 256
#define STUB_SECTOR 4096u
#define STUB_FIRST_BYTE_MS 3000
#define STUB_NEXT_BYTES_MS 200

#define PROBE_VERSION 2
#define REPLY_HEADER 3
#define REPLY_MAX_PAYLOAD (32 - REPLY_HEADER)
#define READ_MAX 24
#define ACK_TIMEOUT_MS 100
#define SYNC_POLL_MS 50
#define LINE_DRIVEN 0xFF

enum probe_cmd {
    CMD_INFO = 0,
    CMD_LINES,
    CMD_BOOT0,
    CMD_NRST,
    CMD_UART,
    CMD_BL_SYNC,
    CMD_BL_QUERY,
    CMD_BL_READ,
    CMD_BL_WRITE,
    CMD_BL_GO,
    CMD_STUB_PING,
    CMD_STUB_XFER,
};

enum probe_status {
    ST_OK = 0,
    ST_BAD_ARG,
    ST_NOT_IN_SESSION,
    ST_TIMEOUT,
    ST_NACK,
};

static bool boot0_driven = false;
static bool nrst_driven  = false;
static bool uart_custom  = false;
static bool stub_active  = false;

static uint8_t  stream_frame[8];
static uint16_t stream_len = 0;

static SerialConfig uart_config = {
    115200, UART_WRDLEN, UART_STPBIT, UART_PARITY, UART_ATFLCT,
};

bool screen_probe_active(void) {
    return boot0_driven || nrst_driven || uart_custom;
}

static void uart_flush_input(void) {
    while (sdGetTimeout(&SD3, TIME_IMMEDIATE) != MSG_TIMEOUT) {
    }
}

static void rx_restore(void) {
    palSetLineMode(SD3_RX_PIN, PAL_MODE_ALTERNATE(UART_RX_PAL_MODE) | PAL_OUTPUT_TYPE_PUSHPULL | PAL_OUTPUT_SPEED_HIGHEST);
    uart_flush_input();
}

static uint8_t read_pulled(pin_t pin, bool pull_up) {
    if (pull_up) {
        gpio_set_pin_input_high(pin);
    } else {
        gpio_set_pin_input_low(pin);
    }
    wait_ms(1);
    return gpio_read_pin(pin) ? 1 : 0;
}

/*
 * The weak pulls are about 40k, so a line stays at the level of a 10k external
 * pull whichever way the internal pull goes, and follows the internal pull if
 * nothing external holds it.
 */
static uint8_t cmd_lines(uint8_t *out) {
    memset(out, LINE_DRIVEN, 4);
    if (!boot0_driven) {
        out[0] = read_pulled(PROBE_BOOT0_PIN, true);
        out[1] = read_pulled(PROBE_BOOT0_PIN, false);
        gpio_set_pin_input(PROBE_BOOT0_PIN);
    }
    if (!nrst_driven) {
        out[2] = read_pulled(PROBE_NRST_PIN, true);
        out[3] = read_pulled(PROBE_NRST_PIN, false);
        gpio_set_pin_input(PROBE_NRST_PIN);
    }
    out[4] = read_pulled(SD3_RX_PIN, true);
    out[5] = read_pulled(SD3_RX_PIN, false);
    rx_restore();
    return ST_OK;
}

static uint8_t cmd_boot0(bool high) {
    if (high) {
        gpio_set_pin_output(PROBE_BOOT0_PIN);
        gpio_write_pin_high(PROBE_BOOT0_PIN);
    } else {
        gpio_set_pin_input(PROBE_BOOT0_PIN);
    }
    boot0_driven = high;
    return ST_OK;
}

static uint8_t cmd_nrst(bool assert_reset) {
    if (assert_reset) {
        gpio_set_pin_output_open_drain(PROBE_NRST_PIN);
        gpio_write_pin_low(PROBE_NRST_PIN);
    } else {
        gpio_set_pin_input(PROBE_NRST_PIN);
    }
    nrst_driven = assert_reset;
    if (assert_reset) stub_active = false;
    return ST_OK;
}

static uint8_t cmd_uart(const uint8_t *args) {
    uint32_t baud = ((uint32_t)args[0] << 24) | ((uint32_t)args[1] << 16) | ((uint32_t)args[2] << 8) | args[3];
    if (baud < 1200 || baud > 3000000 || args[4] > 1) return ST_BAD_ARG;

    uart_config.speed       = baud;
    uart_config.UART_Parity = args[4] ? UART_Parity_Even : UART_PARITY;

    sdStop(&SD3);
    sdStart(&SD3, &uart_config);
    uart_custom = args[5] != 0;
    if (!uart_custom) stub_active = false;
    return ST_OK;
}

static msg_t uart_get(uint32_t timeout_ms) {
    return sdGetTimeout(&SD3, TIME_MS2I(timeout_ms));
}

static uint8_t wait_ack(void) {
    msg_t b = uart_get(ACK_TIMEOUT_MS);
    if (b == MSG_TIMEOUT || b == MSG_RESET) return ST_TIMEOUT;
    return b == BL_ACK ? ST_OK : ST_NACK;
}

static void send_with_complement(uint8_t b) {
    uint8_t frame[2] = {b, (uint8_t)~b};
    sdWrite(&SD3, frame, sizeof(frame));
}

/* Replies raw bytes so the host can see an unexpected protocol */
static uint8_t read_until_idle(uint8_t *out, uint8_t *count, uint32_t first_ms) {
    *count = 0;
    while (*count < REPLY_MAX_PAYLOAD - 1) {
        msg_t b = uart_get(*count ? 10 : first_ms);
        if (b == MSG_TIMEOUT || b == MSG_RESET) break;
        out[(*count)++] = (uint8_t)b;
    }
    return *count ? ST_OK : ST_TIMEOUT;
}

static uint8_t bl_sync(uint8_t *out, uint32_t timeout_ms) {
    if (!uart_custom) return ST_NOT_IN_SESSION;
    if (stub_active) return ST_NOT_IN_SESSION;

    uart_flush_input();
    uint8_t sync = BL_SYNC_BYTE;
    sdWrite(&SD3, &sync, 1);

    return read_until_idle(out + 1, out, timeout_ms);
}

static uint8_t cmd_bl_sync(uint8_t *out) {
    return bl_sync(out, ACK_TIMEOUT_MS);
}

static uint8_t cmd_bl_query(uint8_t cmd, uint8_t *out) {
    if (!uart_custom) return ST_NOT_IN_SESSION;
    if (stub_active) return ST_NOT_IN_SESSION;
    if (cmd != BL_CMD_GET && cmd != BL_CMD_GET_VERSION && cmd != BL_CMD_GET_ID) return ST_BAD_ARG;

    uart_flush_input();
    send_with_complement(cmd);
    return read_until_idle(out + 1, out, ACK_TIMEOUT_MS);
}

/* out[0] holds the failing step on error, or the byte count on success */
static uint8_t cmd_bl_read(const uint8_t *args, uint8_t *out) {
    if (!uart_custom) return ST_NOT_IN_SESSION;
    if (stub_active) return ST_NOT_IN_SESSION;

    uint8_t len = args[4];
    if (len == 0 || len > READ_MAX) return ST_BAD_ARG;

    uart_flush_input();

    out[0] = 1;
    send_with_complement(BL_CMD_READ_MEMORY);
    uint8_t st = wait_ack();
    if (st != ST_OK) return st;

    uint8_t frame[5];
    memcpy(frame, args, 4);
    frame[4] = args[0] ^ args[1] ^ args[2] ^ args[3];
    out[0]   = 2;
    sdWrite(&SD3, frame, sizeof(frame));
    st = wait_ack();
    if (st != ST_OK) return st;

    out[0] = 3;
    send_with_complement(len - 1);
    st = wait_ack();
    if (st != ST_OK) return st;

    for (uint8_t i = 0; i < len; i++) {
        msg_t b = uart_get(ACK_TIMEOUT_MS);
        if (b == MSG_TIMEOUT || b == MSG_RESET) {
            out[0] = 4;
            return ST_TIMEOUT;
        }
        out[i + 1] = (uint8_t)b;
    }
    out[0] = len;
    return ST_OK;
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t read_le32(const uint8_t *p) {
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}

static void send_address(const uint8_t *addr) {
    uint8_t frame[5];
    memcpy(frame, addr, 4);
    frame[4] = addr[0] ^ addr[1] ^ addr[2] ^ addr[3];
    sdWrite(&SD3, frame, sizeof(frame));
}

/* Only SRAM can be written. out[0] holds the failing step */
static uint8_t bl_write(const uint8_t *addr_be, const uint8_t *data, uint8_t len, uint8_t *out) {
    if (!uart_custom || stub_active) return ST_NOT_IN_SESSION;

    uint32_t addr = read_be32(addr_be);
    if (len == 0 || len > SCREEN_BL_WRITE_MAX || (len & 3) != 0) return ST_BAD_ARG;
    if (addr < SRAM_START || addr >= SRAM_END || len > SRAM_END - addr) return ST_BAD_ARG;

    uart_flush_input();

    out[0] = 1;
    send_with_complement(BL_CMD_WRITE_MEMORY);
    uint8_t st = wait_ack();
    if (st != ST_OK) return st;

    out[0] = 2;
    send_address(addr_be);
    st = wait_ack();
    if (st != ST_OK) return st;

    uint8_t count    = len - 1;
    uint8_t checksum = count;
    for (uint8_t i = 0; i < len; i++) {
        checksum ^= data[i];
    }
    out[0] = 3;
    sdWrite(&SD3, &count, 1);
    sdWrite(&SD3, data, len);
    sdWrite(&SD3, &checksum, 1);
    return wait_ack();
}

/* args: addr (u32 BE), len, data */
static uint8_t cmd_bl_write(const uint8_t *args, uint8_t *out) {
    if (args[4] > READ_MAX) return ST_BAD_ARG;
    return bl_write(args, args + 5, args[4], out);
}

/* args: addr (u32 BE). out[0] holds the failing step */
static uint8_t cmd_bl_go(const uint8_t *args, uint8_t *out) {
    if (!uart_custom || stub_active) return ST_NOT_IN_SESSION;

    uint32_t addr = read_be32(args);
    if (addr < SRAM_START || addr >= SRAM_END) return ST_BAD_ARG;

    uart_flush_input();

    out[0] = 1;
    send_with_complement(BL_CMD_GO);
    uint8_t st = wait_ack();
    if (st != ST_OK) return st;

    out[0] = 2;
    send_address(args);
    return wait_ack();
}

static void stub_send_frame(uint8_t op, uint32_t addr, uint16_t len) {
    uint8_t frame[8] = {
        STUB_FRAME_MAGIC, op, addr & 0xFF, (addr >> 8) & 0xFF, (addr >> 16) & 0xFF, addr >> 24, len & 0xFF, len >> 8,
    };
    sdWrite(&SD3, frame, sizeof(frame));
}

static uint8_t cmd_stub_ping(uint8_t *out) {
    static const uint8_t magic[4] = {'S', 'T', 'U', 'B'};

    if (!uart_custom) return ST_NOT_IN_SESSION;

    uart_flush_input();
    stub_send_frame(STUB_OP_PING, 0, 0);
    if (sdReadTimeout(&SD3, out, 8, TIME_MS2I(ACK_TIMEOUT_MS)) != 8 || memcmp(out, magic, 4) != 0) {
        return ST_TIMEOUT;
    }
    stub_active = true;
    return ST_OK;
}

/* args: op, addr (u32 LE), len (u16 LE). The reply is streamed by screen_probe_stream */
static uint8_t cmd_stub_xfer(const uint8_t *args) {
    if (!stub_active) return ST_NOT_IN_SESSION;

    uint8_t  op   = args[0];
    uint32_t addr = read_le32(args + 1);
    uint16_t len  = args[5] | ((uint16_t)args[6] << 8);
    uint16_t rx_len;

    if (addr < EXT_FLASH_START || addr >= EXT_FLASH_END) return ST_BAD_ARG;

    if (op == STUB_OP_READ) {
        if (len == 0 || len > STUB_READ_MAX || (len & 3) != 0 || (addr & 3) != 0) return ST_BAD_ARG;
        if (len > EXT_FLASH_END - addr) return ST_BAD_ARG;
        rx_len = len + 8;
    } else if (op == STUB_OP_MAP) {
        if (len == 0 || len > STUB_MAP_MAX || (addr & (STUB_SECTOR - 1)) != 0) return ST_BAD_ARG;
        if (len * STUB_SECTOR > EXT_FLASH_END - addr) return ST_BAD_ARG;
        rx_len = (len + 7) / 8 + 4;
    } else {
        return ST_BAD_ARG;
    }

    uint8_t frame[8] = {
        STUB_FRAME_MAGIC, op, addr & 0xFF, (addr >> 8) & 0xFF, (addr >> 16) & 0xFF, addr >> 24, len & 0xFF, len >> 8,
    };
    memcpy(stream_frame, frame, sizeof(frame));
    stream_len = rx_len;
    return ST_OK;
}

/*
 * Forwards one reply from the SRAM program as whole 32 byte reports with no
 * header. If it stalls the rest is zero padded, so the host always receives the
 * expected count and the CRC check fails.
 */
void screen_probe_stream(screen_probe_send_t send) {
    if (stream_len == 0) return;

    uint16_t remaining = stream_len;
    sysinterval_t wait = TIME_MS2I(STUB_FIRST_BYTE_MS);
    bool     stalled   = false;
    stream_len         = 0;

    uart_flush_input();
    sdWrite(&SD3, stream_frame, sizeof(stream_frame));

    while (remaining > 0) {
        uint8_t report[32] = {0};
        uint8_t want       = remaining < sizeof(report) ? remaining : sizeof(report);
        if (!stalled) {
            size_t got = sdReadTimeout(&SD3, report, want, wait);
            wait       = TIME_MS2I(STUB_NEXT_BYTES_MS);
            if (got != want) stalled = true;
        }
        send(report, sizeof(report));
        remaining -= want;
    }
}

void screen_bl_reset(bool asserted) {
    cmd_nrst(asserted);
}

void screen_bl_uart(uint32_t baud, bool even_parity, bool session) {
    const uint8_t args[6] = {baud >> 24, (baud >> 16) & 0xFF, (baud >> 8) & 0xFF, baud & 0xFF, even_parity, session};
    cmd_uart(args);
}

/* Stock firmware flags a backup register and resets into its ROM bootloader, with no reply */
void screen_bl_enter_from_stock(void) {
    static const uint8_t frame[3] = {0xA5, 0x00, 0xFE};
    sdWrite(&SD3, frame, sizeof(frame));
}

bool screen_bl_sync(void) {
    uint8_t out[REPLY_MAX_PAYLOAD];
    return bl_sync(out, SYNC_POLL_MS) == ST_OK && (out[1] == BL_ACK || out[1] == BL_NACK);
}

bool screen_bl_write(uint32_t address, const uint8_t *data, uint8_t length) {
    const uint8_t addr[4] = {address >> 24, (address >> 16) & 0xFF, (address >> 8) & 0xFF, address & 0xFF};
    uint8_t       out[REPLY_MAX_PAYLOAD];
    return bl_write(addr, data, length, out) == ST_OK;
}

bool screen_bl_go(uint32_t address) {
    const uint8_t args[4] = {address >> 24, (address >> 16) & 0xFF, (address >> 8) & 0xFF, address & 0xFF};
    uint8_t       out[REPLY_MAX_PAYLOAD];
    return cmd_bl_go(args, out) == ST_OK;
}

bool screen_bl_poll_ready(uint8_t ready_byte) {
    if (!uart_custom || sdGetTimeout(&SD3, TIME_IMMEDIATE) != ready_byte) return false;
    stub_active = true;
    return true;
}

bool screen_stage_begin(uint32_t address, uint16_t length) {
    if (!stub_active || address < SRAM_START || address >= SRAM_END || length > SRAM_END - address) return false;
    uart_flush_input();
    stub_send_frame(STAGE_OP_LOAD, address, length);
    return true;
}

void screen_stage_data(const uint8_t *data, uint16_t length) {
    sdWrite(&SD3, data, length);
}

bool screen_stage_crc(uint32_t *crc) {
    uint8_t reply[4];
    if (sdReadTimeout(&SD3, reply, sizeof(reply), TIME_MS2I(ACK_TIMEOUT_MS)) != sizeof(reply)) return false;
    *crc = read_le32(reply);
    return true;
}

bool screen_stage_go(uint32_t address) {
    if (!stub_active || address < SRAM_START || address >= SRAM_END) return false;
    stub_send_frame(STAGE_OP_GO, address, 0);
    return true;
}

void screen_probe_command(const uint8_t *data, uint8_t *reply) {
    const uint8_t *args = data + 2;
    uint8_t       *out  = reply + REPLY_HEADER;
    uint8_t        st;

    memset(reply, 0, 32);
    reply[0] = SCREEN_PROBE_REPORT_ID;
    reply[1] = data[1];

    switch (data[1]) {
        case CMD_INFO:
            out[0] = PROBE_VERSION;
            out[1] = screen_probe_active() ? 1 : 0;
#ifdef LCDFW_ENABLE
            lcdfw_load_report(out + 2);
#endif
            st     = ST_OK;
            break;
        case CMD_LINES:
            st = cmd_lines(out);
            break;
        case CMD_BOOT0:
            st = cmd_boot0(args[0] != 0);
            break;
        case CMD_NRST:
            st = cmd_nrst(args[0] != 0);
            break;
        case CMD_UART:
            st = cmd_uart(args);
            break;
        case CMD_BL_SYNC:
            st = cmd_bl_sync(out);
            break;
        case CMD_BL_QUERY:
            st = cmd_bl_query(args[0], out);
            break;
        case CMD_BL_READ:
            st = cmd_bl_read(args, out);
            break;
        case CMD_BL_WRITE:
            st = cmd_bl_write(args, out);
            break;
        case CMD_BL_GO:
            st = cmd_bl_go(args, out);
            break;
        case CMD_STUB_PING:
            st = cmd_stub_ping(out);
            break;
        case CMD_STUB_XFER:
            st = cmd_stub_xfer(args);
            break;
        default:
            st = ST_BAD_ARG;
            break;
    }
    reply[2] = st;
}
