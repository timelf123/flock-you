#include "fy_board.h"



#if defined(FY_HAS_DISPLAY)



#include "fy_display_hw.h"



#include <Arduino.h>

#include <Wire.h>

#include <TFT_eSPI.h>

#include <lvgl.h>



static TFT_eSPI s_tft;

static lv_display_t* s_disp = nullptr;

static lv_indev_t* s_touchIndev = nullptr;

static bool s_hwReady = false;



static void lcdBacklightOn() {

    pinMode(FY_LCD_BL_PIN, OUTPUT);

    digitalWrite(FY_LCD_BL_PIN, HIGH);

}



#if defined(FY_HAS_TOUCH) && FY_HAS_TOUCH



#define FT6336_REG_TD_STATUS  0x02

#define FT6336_REG_P1_XH      0x03

#define FT6336_REG_CHIP_ID    0xA3

#define FT6336_REG_THRESHOLD  0x80

#define FT6336_REG_ACTIVE_RATE 0x88



static bool s_touchReady = false;



static bool ft6336Write(uint8_t reg, uint8_t val) {

    Wire.beginTransmission(FY_TOUCH_I2C_ADDR);

    Wire.write(reg);

    Wire.write(val);

    return Wire.endTransmission() == 0;

}



static bool ft6336Read(uint8_t reg, uint8_t* buf, uint8_t len) {

    Wire.beginTransmission(FY_TOUCH_I2C_ADDR);

    Wire.write(reg);

    if (Wire.endTransmission(false) != 0) return false;

    if (Wire.requestFrom((uint8_t)FY_TOUCH_I2C_ADDR, len) != len) return false;

    for (uint8_t i = 0; i < len; i++) {

        buf[i] = Wire.read();

    }

    return true;

}



static void i2cScanLog() {

    printf("[FLOCK-YOU] I2C scan SDA=%d SCL=%d:\n", FY_TOUCH_SDA_PIN, FY_TOUCH_SCL_PIN);

    uint8_t found = 0;

    for (uint8_t addr = 1; addr < 127; addr++) {

        Wire.beginTransmission(addr);

        if (Wire.endTransmission() == 0) {

            printf("[FLOCK-YOU]   device 0x%02X\n", addr);

            found++;

        }

    }

    if (!found) printf("[FLOCK-YOU]   (no devices found)\n");

}



static void touchReset() {

    pinMode(FY_TOUCH_RST_PIN, OUTPUT);

    digitalWrite(FY_TOUCH_RST_PIN, LOW);

    delay(10);

    digitalWrite(FY_TOUCH_RST_PIN, HIGH);

    delay(200);

}



static void touchMap(uint16_t rawX, uint16_t rawY, uint16_t* x, uint16_t* y) {

#if FY_TOUCH_SWAP_XY

    uint16_t tx = rawY;

    uint16_t ty = rawX;

#else

    uint16_t tx = rawX;

    uint16_t ty = rawY;

#endif

#if FY_TOUCH_MIRROR_X

    if (tx >= FY_SCREEN_W) tx = FY_SCREEN_W - 1;

    tx = (FY_SCREEN_W - 1) - tx;

#endif

#if FY_TOUCH_MIRROR_Y

    if (ty >= FY_SCREEN_H) ty = FY_SCREEN_H - 1;

    ty = (FY_SCREEN_H - 1) - ty;

#endif

    if (tx >= FY_SCREEN_W) tx = FY_SCREEN_W - 1;

    if (ty >= FY_SCREEN_H) ty = FY_SCREEN_H - 1;

    *x = tx;

    *y = ty;

}



static bool touchInit() {

    touchReset();



    uint8_t id = 0;

    if (!ft6336Read(FT6336_REG_CHIP_ID, &id, 1)) {

        printf("[FLOCK-YOU] Touch I2C read failed (addr 0x%02X)\n", FY_TOUCH_I2C_ADDR);

        return false;

    }

    printf("[FLOCK-YOU] Touch chip ID: 0x%02X\n", id);



    ft6336Write(FT6336_REG_THRESHOLD, 22);

    ft6336Write(FT6336_REG_ACTIVE_RATE, 0x0E);



    s_touchReady = true;

    return true;

}



static bool touchReadRaw(uint16_t* x, uint16_t* y) {

    if (!s_touchReady) return false;



    uint8_t status = 0;

    if (!ft6336Read(FT6336_REG_TD_STATUS, &status, 1)) return false;

    if ((status & 0x0F) == 0) return false;



    uint8_t data[4];

    if (!ft6336Read(FT6336_REG_P1_XH, data, 4)) return false;



    uint16_t rawX = ((data[0] & 0x0F) << 8) | data[1];

    uint16_t rawY = ((data[2] & 0x0F) << 8) | data[3];

    touchMap(rawX, rawY, x, y);

    return true;

}



static void touchReadCb(lv_indev_t* indev, lv_indev_data_t* data) {

    (void)indev;

    uint16_t x = 0, y = 0;
    static uint16_t s_lastX = 0;
    static uint16_t s_lastY = 0;

    if (touchReadRaw(&x, &y)) {

        data->state = LV_INDEV_STATE_PRESSED;

        s_lastX = x;
        s_lastY = y;
        data->point.x = (int32_t)s_lastX;
        data->point.y = (int32_t)s_lastY;

#if defined(FY_TOUCH_DEBUG) && FY_TOUCH_DEBUG

        static uint32_t s_lastLog = 0;

        uint32_t now = millis();

        if (now - s_lastLog > 500) {

            s_lastLog = now;

            printf("[FLOCK-YOU] touch %ld,%ld\n", (long)data->point.x, (long)data->point.y);

        }

#endif

    } else {

        data->state = LV_INDEV_STATE_RELEASED;
        // LVGL uses the release coordinates to decide if a CLICKED event should fire.
        // If we don't provide them, clicks can be missed.
        data->point.x = (int32_t)s_lastX;
        data->point.y = (int32_t)s_lastY;

    }

}



#endif



static void lvglFlush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {

    const uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);

    const uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);

    s_tft.startWrite();

    s_tft.setAddrWindow(area->x1, area->y1, w, h);

    s_tft.pushPixels((uint16_t*)px_map, w * h);

    s_tft.endWrite();

    lv_display_flush_ready(disp);

}



bool fyDisplayHwInit() {

    if (s_hwReady) return true;



    printf("[FLOCK-YOU] LCD ILI9341 CS=%d DC=%d MOSI=%d SCLK=%d BL=%d\n",

           FY_LCD_CS_PIN, FY_LCD_DC_PIN, FY_LCD_MOSI_PIN, FY_LCD_SCLK_PIN, FY_LCD_BL_PIN);



    lcdBacklightOn();



    s_tft.init();

    s_tft.setRotation(0);

    s_tft.invertDisplay(false);



    s_tft.fillScreen(0x1084);



#if defined(FY_HAS_TOUCH) && FY_HAS_TOUCH

    Wire.begin(FY_TOUCH_SDA_PIN, FY_TOUCH_SCL_PIN);

    Wire.setClock(400000);

    pinMode(FY_TOUCH_INT_PIN, INPUT_PULLUP);

    delay(10);

    i2cScanLog();

    if (!touchInit()) {

        printf("[FLOCK-YOU] WARNING: touch init failed (no FT6336 at 0x%02X?)\n",

               FY_TOUCH_I2C_ADDR);

    } else {

        printf("[FLOCK-YOU] Touch FT6336 OK (SDA=%d SCL=%d INT=%d RST=%d swap=%d)\n",

               FY_TOUCH_SDA_PIN, FY_TOUCH_SCL_PIN, FY_TOUCH_INT_PIN, FY_TOUCH_RST_PIN,

               FY_TOUCH_SWAP_XY);

    }

#endif



    lv_init();

    // LVGL needs a millisecond tick source. Without this, timers/input may not run.
    lv_tick_set_cb([]() -> uint32_t { return (uint32_t)millis(); });
    lv_delay_set_cb([](uint32_t ms) { delay(ms); });



    const uint32_t bufPixels = FY_SCREEN_W * 12;
    lv_color_t* buf1 = (lv_color_t*)heap_caps_malloc(
        bufPixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf1) {
        buf1 = (lv_color_t*)malloc(bufPixels * sizeof(lv_color_t));
    }

    if (!buf1) {

        printf("[FLOCK-YOU] LCD framebuffer alloc failed\n");

        return false;

    }



    s_disp = lv_display_create(FY_SCREEN_W, FY_SCREEN_H);

    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);

    lv_display_set_flush_cb(s_disp, lvglFlush);

    lv_display_set_buffers(s_disp, buf1, nullptr, bufPixels * sizeof(lv_color_t),

                           LV_DISPLAY_RENDER_MODE_PARTIAL);



#if defined(FY_HAS_TOUCH) && FY_HAS_TOUCH

    if (s_touchReady) {

        s_touchIndev = lv_indev_create();

        lv_indev_set_type(s_touchIndev, LV_INDEV_TYPE_POINTER);

        lv_indev_set_read_cb(s_touchIndev, touchReadCb);

        lv_indev_set_display(s_touchIndev, s_disp);

    }

#endif



    s_hwReady = true;

    printf("[FLOCK-YOU] LCD hardware init OK\n");

    return true;

}



lv_display_t* fyDisplayGetLvglDisplay() {

    return s_disp;

}



void fyDisplayHwTick() {

    lv_timer_handler();

}



#endif

