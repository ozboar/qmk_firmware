// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "uart.h"
#include "module.h"
#ifdef RAW_ENABLE
#    include "raw_hid.h"
#endif

#define MODULE_BAUD 115200
#define QUEUE_LEN 24
#define FRAME_MAX (MODULE_RAW_LEN + 4)
#define NAME_MAX 18

#define ACK_TIMEOUT_MS 6
#define QUEUE_WAIT_MS 500
#define RAW_RX_TIMEOUT_MS 50
#define SEND_ATTEMPTS 4

/* Frames to the module */
#define TX_KEYBOARD 0xA1
#define TX_NKRO 0xA2
#define TX_CONSUMER 0xA3
#define TX_SYSTEM 0xA4
#define TX_CONTROL 0xA6
#define TX_MOUSE 0xA8
#define TX_NAME 0xA9
#define TX_RAW 0xAF
#define TX_RAW_DIRECTION 0x61

/* Frames from the module */
#define RX_LEDS 0x5A
#define RX_EVENT 0x5B
#define RX_BATTERY 0x5C
#define RX_VERSION 0x5D
#define RX_HOST_STATE 0x60
#define RX_RAW 0xAF
#define RX_RAW_DIRECTION 0x60

/* Second byte of an RX_EVENT frame */
#define EVENT_BATTERY_LOW 0x21
#define EVENT_BATTERY_OFF 0x22
#define EVENT_BATTERY_NORMAL 0x23
#define EVENT_CONNECTED 0x32
#define EVENT_DISCONNECTED 0x33
#define EVENT_REJECTED 0x36
#define EVENT_RESEND 0x42
#define EVENT_DROP 0x43

#define BATTERY_LOW_PERCENT 10
#define BATTERY_OFF_PERCENT 1

typedef struct {
    uint8_t len;
    uint8_t data[FRAME_MAX];
} frame_t;

static frame_t queue[QUEUE_LEN];
static uint8_t queue_head;
static uint8_t queue_tail;

static bool     awaiting_ack;
static uint8_t  send_attempts;
static uint32_t send_timer;

static uint8_t window[3];
static uint8_t window_fill;

#ifdef RAW_ENABLE
static uint8_t  raw_rx[MODULE_RAW_LEN];
static uint8_t  raw_rx_pos;
static uint8_t  raw_rx_sum;
static bool     raw_rx_active;
static uint32_t raw_rx_timer;
#endif

static bool    connected;
static uint8_t battery = 100;
static bool    battery_critical;
static uint8_t leds;

static uint8_t checksum(const uint8_t *data, uint8_t len) {
    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

static void send_next(void) {
    if (awaiting_ack || queue_head == queue_tail) {
        return;
    }
    uart_transmit(queue[queue_head].data, queue[queue_head].len);
    awaiting_ack = true;
    send_timer   = timer_read32();
}

static void pump(void);
static bool pumping;

static bool queue_full(void) {
    return (queue_tail + 1) % QUEUE_LEN == queue_head;
}

/* A macro sends faster than the link drains, so wait for room rather than lose its keys */
static bool wait_for_room(void) {
    const uint32_t timer = timer_read32();

    while (queue_full() && !pumping && timer_elapsed32(timer) < QUEUE_WAIT_MS) {
        pump();
    }
    return !queue_full();
}

static bool enqueue(uint8_t type, const uint8_t *payload, uint8_t len) {
    if (len + 2 > FRAME_MAX || !wait_for_room()) {
        return false;
    }

    const uint8_t next  = (queue_tail + 1) % QUEUE_LEN;
    frame_t      *frame = &queue[queue_tail];
    frame->data[0]      = type;
    memcpy(frame->data + 1, payload, len);
    frame->data[len + 1] = checksum(frame->data, len + 1);
    frame->len           = len + 2;
    queue_tail           = next;

    send_next();
    return true;
}

/* Only ever acts on a frame that was sent, so a stray reply cannot lose the one behind it */
static void finish_current(void) {
    if (!awaiting_ack) {
        return;
    }
    queue_head    = (queue_head + 1) % QUEUE_LEN;
    awaiting_ack  = false;
    send_attempts = 0;
}

/* Gives up on a frame after a few tries, so one lost frame cannot block everything behind it */
static void retry_current(void) {
    if (!awaiting_ack) {
        return;
    }
    if (++send_attempts >= SEND_ATTEMPTS) {
        finish_current();
    } else {
        awaiting_ack = false;
    }
}

static void send_ack(void) {
    static const uint8_t ack[] = {0x61, 0x0D, 0x0A};
    uart_transmit(ack, sizeof(ack));
}

static void handle_event(uint8_t event) {
    switch (event) {
        case EVENT_BATTERY_LOW:
            battery = BATTERY_LOW_PERCENT;
            break;
        case EVENT_BATTERY_OFF:
            battery          = BATTERY_OFF_PERCENT;
            battery_critical = true;
            break;
        case EVENT_BATTERY_NORMAL:
            battery_critical = false;
            module_send_control(MODULE_CMD_BATTERY);
            break;
        case EVENT_CONNECTED:
            connected = true;
            break;
        case EVENT_DISCONNECTED:
            connected = false;
            break;
        case EVENT_REJECTED:
            module_send_control(MODULE_CMD_PAIR);
            break;
        default:
            break;
    }
}

static bool is_frame_type(uint8_t type) {
    return type == RX_LEDS || type == RX_EVENT || type == RX_BATTERY || type == RX_VERSION || type == RX_HOST_STATE;
}

static void handle_frame(uint8_t type, uint8_t value) {
    if (type == RX_EVENT && value == EVENT_RESEND) {
        retry_current();
        return;
    }
    if (type == RX_EVENT && value == EVENT_DROP) {
        finish_current();
        return;
    }

    send_ack();
    switch (type) {
        case RX_LEDS:
            if (value < 8) {
                leds = value;
            }
            break;
        case RX_EVENT:
            handle_event(value);
            break;
        case RX_BATTERY:
            battery = value;
            break;
        default:
            break;
    }
}

#ifdef RAW_ENABLE
static void raw_rx_byte(uint8_t byte) {
    if (raw_rx_pos < MODULE_RAW_LEN) {
        raw_rx[raw_rx_pos++] = byte;
        raw_rx_sum += byte;
        return;
    }
    raw_rx_active = false;
    if (byte == raw_rx_sum) {
        send_ack();
        raw_hid_receive(raw_rx, MODULE_RAW_LEN);
    }
}
#endif

/* A sliding window finds the next valid frame again after any corrupted byte */
static void rx_byte(uint8_t byte) {
#ifdef RAW_ENABLE
    if (raw_rx_active) {
        raw_rx_byte(byte);
        return;
    }
#endif

    if (window_fill == sizeof(window)) {
        window[0]   = window[1];
        window[1]   = window[2];
        window_fill = 2;
    }
    window[window_fill++] = byte;
    if (window_fill < sizeof(window)) {
        return;
    }

    if (window[0] == 0x61 && window[1] == 0x0D && window[2] == 0x0A) {
        window_fill = 0;
        finish_current();
        return;
    }

#ifdef RAW_ENABLE
    if (window[0] == RX_RAW && window[1] == RX_RAW_DIRECTION && window[2] == MODULE_RAW_LEN) {
        window_fill   = 0;
        raw_rx_pos    = 0;
        raw_rx_sum    = checksum(window, sizeof(window));
        raw_rx_timer  = timer_read32();
        raw_rx_active = true;
        return;
    }
#endif

    if (is_frame_type(window[0]) && window[2] == (uint8_t)(window[0] + window[1])) {
        window_fill = 0;
        handle_frame(window[0], window[1]);
    }
}

void module_init(void) {
    uart_init(MODULE_BAUD);
}

static void pump(void) {
    pumping = true;

    /* Receive first, so a slow main loop cannot turn a waiting ack into a timeout */
    while (uart_available()) {
#ifdef RAW_ENABLE
        raw_rx_timer = timer_read32();
#endif
        rx_byte(uart_read());
    }

#ifdef RAW_ENABLE
    /* A byte lost from a raw frame must not swallow the frames after it */
    if (raw_rx_active && timer_elapsed32(raw_rx_timer) >= RAW_RX_TIMEOUT_MS) {
        raw_rx_active = false;
    }
#endif

    if (awaiting_ack && timer_elapsed32(send_timer) >= ACK_TIMEOUT_MS) {
        retry_current();
    }
    send_next();

    pumping = false;
}

void module_task(void) {
    pump();
}

bool module_send_control(uint8_t command) {
    return enqueue(TX_CONTROL, &command, 1);
}

bool module_send_keyboard(const uint8_t *report) {
    return enqueue(TX_KEYBOARD, report, MODULE_KEYBOARD_LEN);
}

bool module_send_nkro(const uint8_t *bits) {
    return enqueue(TX_NKRO, bits, MODULE_NKRO_LEN);
}

bool module_send_consumer(uint16_t usage) {
    const uint8_t payload[] = {usage & 0xFF, usage >> 8};
    return enqueue(TX_CONSUMER, payload, sizeof(payload));
}

bool module_send_system(uint8_t bits) {
    return enqueue(TX_SYSTEM, &bits, 1);
}

bool module_send_mouse(const uint8_t *report) {
    return enqueue(TX_MOUSE, report, MODULE_MOUSE_LEN);
}

bool module_send_name(const char *name) {
    uint8_t payload[NAME_MAX + 1];
    uint8_t len = strlen(name) + 1;

    if (len > NAME_MAX) {
        return false;
    }
    payload[0] = len;
    memcpy(payload + 1, name, len);
    return enqueue(TX_NAME, payload, len + 1);
}

bool module_send_raw(const uint8_t *data) {
    uint8_t payload[MODULE_RAW_LEN + 2] = {TX_RAW_DIRECTION, MODULE_RAW_LEN};
    memcpy(payload + 2, data, MODULE_RAW_LEN);
    return enqueue(TX_RAW, payload, sizeof(payload));
}

void module_reset_link(void) {
    connected = false;
    leds      = 0;
}

bool module_connected(void) {
    return connected;
}

uint8_t module_battery(void) {
    return battery;
}

bool module_battery_critical(void) {
    return battery_critical;
}

void module_clear_battery_critical(void) {
    battery_critical = false;
}

bool module_busy(void) {
    return queue_head != queue_tail;
}

uint8_t module_leds(void) {
    return leds;
}
