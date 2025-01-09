// Copyright 2024 MHooijberg <https://github.com/MHooijberg>
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// External clock configuration
#undef AT32_HEXTCLK
#define AT32_HEXTCLK 16000000

// External Flash Configuration.
#define EXTERNAL_FLASH_ADDRESS_SIZE 3
#define EXTERNAL_FLASH_BLOCK_SIZE 65536
#define EXTERNAL_FLASH_PAGE_SIZE 256
#define EXTERNAL_FLASH_SECTOR_SIZE 4096
#define EXTERNAL_FLASH_SIZE 524288
#define EXTERNAL_FLASH_SPI_CLOCK_DIVISOR 4

// Define SPI config for AT32F415 Implementation.
#define SPI_CS_PIN   B12
#define SPI_DRIVER SPID2
#define SPI_MISO_PIN B14
#define SPI_MOSI_PIN B15
#define SPI_SCLK_PIN B13

// Wear leveling configuration.
#define WEAR_LEVELING_EXTERNAL_FLASH_BLOCK_COUNT	1
#define WEAR_LEVELING_EXTERNAL_FLASH_BLOCK_OFFSET	0
#define WEAR_LEVELING_LOGICAL_SIZE	32768
#define WEAR_LEVELING_BACKING_SIZE	65536
#define BACKING_STORE_WRITE_SIZE
