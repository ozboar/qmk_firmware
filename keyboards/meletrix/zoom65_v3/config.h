#pragma once

/* External flash, holds the wear leveled EEPROM */
#define SPI_DRIVER SPIDQ
#define SPI_SCK_PIN B3
#define SPI_MOSI_PIN B5
#define SPI_MISO_PIN B4
#define SPI_MOSI_PAL_MODE 5
#define EXTERNAL_FLASH_SPI_SLAVE_SELECT_PIN C12

/* UART1 to the wireless module */
#define UART_DRIVER SD1
#define UART_TX_PIN A9
#define UART_RX_PIN A10

/* UART line settings, also used for UART3 to the screen */
#define UART_TX_PAL_MODE 7
#define UART_RX_PAL_MODE 7
#define UART_WRDLEN 3
#define UART_STPBIT 0
#define UART_PARITY 0
#define UART_ATFLCT 0

#define SD3_TX_PIN C10
#define SD3_RX_PIN C11
#define SCREEN_BOOT0_PIN A8
#define SCREEN_NRST_PIN B13

/* Board control */
#define USB_DATA_DISABLE_PIN A14
#define LED_POWER_PIN B7
#define LED_POWER_AUX_PIN B12
#define CABLE_DETECT_PIN B9
#define BATTERY_FULL_PIN B8
#define WIRELESS_SWITCH_PIN C0

#define CONNECTION_HOST_DEFAULT CONNECTION_HOST_USB
#define USB_SUSPEND_DELAY_MS 3000

#define GRAVE_ESC_CTRL_OVERRIDE
