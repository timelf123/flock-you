// TFT_eSPI — LCDWiki 2.8" ESP32-S3 E32C28P / E32N28P (ILI9341 240x320)
// https://www.lcdwiki.com/2.8inch_ESP32-S3_Display_E32C28P/E32N28P
#pragma once

#define USER_SETUP_LOADED

#define ILI9341_DRIVER

#define TFT_WIDTH 240
#define TFT_HEIGHT 320

#define TFT_MISO 13
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS 10
#define TFT_DC 46
#define TFT_RST -1
#define TFT_BL 45
#define TFT_BACKLIGHT_ON HIGH

#define LOAD_GLCD
#define LOAD_FONT2

#define ESP32
#define CONFIG_IDF_TARGET_ESP32S3

#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 20000000
