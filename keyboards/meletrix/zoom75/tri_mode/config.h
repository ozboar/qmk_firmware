// Copyright 2024 MHooijberg <https://github.com/MHooijberg>
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#undef AT32_HEXTCLK
#define AT32_HEXTCLK 16000000

// Define EEPROM config.
#define EXTERNAL_EEPROM_SPI
#define EXTERNAL_EEPROM_SIZE 0x80000  // 4M-bit (512KB)
#define EXTERNAL_EEPROM_PAGE_SIZE 256

// Define SPI config for AT32F415 Implementation.
#define SPI_SCLK_PIN B13
#define SPI_MOSI_PIN B15
#define SPI_MISO_PIN B14
#define SPI_CS_PIN   B12
