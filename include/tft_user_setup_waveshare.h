// TFT_eSPI — Waveshare ESP32-S3-Touch-LCD-2.8 (ST7789)
#pragma once

#define USER_SETUP_LOADED

#define ST7789_DRIVER
#define TFT_WIDTH 240
#define TFT_HEIGHT 320

#define TFT_MISO -1
#define TFT_MOSI 45
#define TFT_SCLK 40
#define TFT_CS 42
#define TFT_DC 41
#define TFT_RST 39
#define TFT_BL 5
#define TFT_BACKLIGHT_ON HIGH

#define LOAD_GLCD
#define LOAD_FONT2

#define USE_HSPI_PORT
#define ESP32
#define CONFIG_IDF_TARGET_ESP32S3

#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 20000000
