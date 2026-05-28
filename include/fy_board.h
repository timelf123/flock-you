#pragma once

// Seeed XIAO ESP32-C3 (headless piezo)
#if !defined(FY_BOARD_LCDWIKI_E32C28P) && !defined(FY_BOARD_WS_TOUCH_LCD_28)
#define BUZZER_PIN 3
#endif

// LCDWiki 2.8" ESP32-S3 E32C28P / E32N28P (ILI9341 + optional FT6336G touch)
// https://www.lcdwiki.com/2.8inch_ESP32-S3_Display_E32C28P/E32N28P
#if defined(FY_BOARD_LCDWIKI_E32C28P)

#define FY_USE_I2S_AUDIO 1

#define FY_LCD_CS_PIN     10
#define FY_LCD_DC_PIN     46
#define FY_LCD_SCLK_PIN   12
#define FY_LCD_MOSI_PIN   11
#define FY_LCD_MISO_PIN   13
#define FY_LCD_BL_PIN     45
#define FY_LCD_RST_PIN    -1

#define FY_TOUCH_SDA_PIN  16
#define FY_TOUCH_SCL_PIN  15
#define FY_TOUCH_RST_PIN  18
#define FY_TOUCH_INT_PIN  17
#define FY_TOUCH_I2C_ADDR 0x38

#define FY_AUDIO_EN_PIN   1
#define FY_I2S_MCLK_PIN   4
#define FY_I2S_BCLK_PIN   5
#define FY_I2S_DOUT_PIN   6
#define FY_I2S_LRCK_PIN   7

#define FY_RGB_LED_PIN    42

#define FY_SCREEN_W       240
#define FY_SCREEN_H       320

// Touch coords from FT6336 are often landscape-oriented; map to portrait LVGL UI.
#ifndef FY_TOUCH_SWAP_XY
#define FY_TOUCH_SWAP_XY  0
#endif
#ifndef FY_TOUCH_MIRROR_X
#define FY_TOUCH_MIRROR_X 0
#endif
#ifndef FY_TOUCH_MIRROR_Y
#define FY_TOUCH_MIRROR_Y 0
#endif

#endif

// Waveshare-class ESP32-S3-Touch-LCD-2.8 (ST7789) — alternate hardware
#if defined(FY_BOARD_WS_TOUCH_LCD_28)

#define FY_USE_I2S_AUDIO 1

#define FY_LCD_BL_PIN     5
#define FY_LCD_RST_PIN    39
#define FY_LCD_MOSI_PIN   45
#define FY_LCD_SCLK_PIN   40
#define FY_LCD_CS_PIN     42
#define FY_LCD_DC_PIN     41

#define FY_TOUCH_SDA_PIN  1
#define FY_TOUCH_SCL_PIN  3
#define FY_TOUCH_INT_PIN  4
#define FY_TOUCH_RST_PIN  2

#define FY_I2S_BCLK_PIN   48
#define FY_I2S_LRCK_PIN   38
#define FY_I2S_DOUT_PIN   47

#define FY_SCREEN_W       240
#define FY_SCREEN_H       320

#endif
