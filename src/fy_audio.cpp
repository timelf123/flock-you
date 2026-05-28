#include "fy_audio.h"
#include "fy_board.h"
#include <Arduino.h>
#include <math.h>

static bool s_muted = false;

#if defined(FY_USE_I2S_AUDIO)

#include "driver/i2s.h"

static bool s_i2sReady = false;
static const int kSampleRate = 22050;

static void fyI2sInit() {
    if (s_i2sReady) return;

#if defined(FY_AUDIO_EN_PIN)
    pinMode(FY_AUDIO_EN_PIN, OUTPUT);
    digitalWrite(FY_AUDIO_EN_PIN, LOW);
#endif

    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = kSampleRate;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = 0;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = 256;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = true;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = FY_I2S_BCLK_PIN;
    pins.ws_io_num = FY_I2S_LRCK_PIN;
    pins.data_out_num = FY_I2S_DOUT_PIN;
    pins.data_in_num = I2S_PIN_NO_CHANGE;

    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) == ESP_OK &&
        i2s_set_pin(I2S_NUM_0, &pins) == ESP_OK) {
        s_i2sReady = true;
    }
}

static void fyI2sTone(int freq, int durationMs) {
    if (!s_i2sReady || freq < 80 || durationMs <= 0) return;

    const int samples = (kSampleRate * durationMs) / 1000;
    const size_t chunk = 128;
    int16_t buf[chunk];

    for (int offset = 0; offset < samples; offset += (int)chunk) {
        int n = samples - offset;
        if (n > (int)chunk) n = (int)chunk;
        for (int i = 0; i < n; i++) {
            float t = (float)(offset + i) / (float)kSampleRate;
            float s = sinf(2.0f * 3.14159265f * (float)freq * t);
            buf[i] = (int16_t)(s * 12000.0f);
        }
        size_t written = 0;
        i2s_write(I2S_NUM_0, buf, n * sizeof(int16_t), &written, portMAX_DELAY);
    }
}

static void fyBeep(int freq, int dur) {
    if (s_muted) return;
    fyI2sTone(freq, dur);
    delay(dur + 20);
}

static void fyCaw(int startFreq, int endFreq, int durationMs, int warbleHz) {
    if (s_muted) return;
    int steps = durationMs / 8;
    if (steps < 1) steps = 1;
    float fStep = (float)(endFreq - startFreq) / (float)steps;
    for (int i = 0; i < steps; i++) {
        int f = startFreq + (int)(fStep * i);
        if (warbleHz > 0 && (i % 3 == 0)) {
            f += ((i % 6 < 3) ? warbleHz : -warbleHz);
        }
        if (f < 100) f = 100;
        fyI2sTone(f, 8);
        delay(8);
    }
}

#else

static void fyBeep(int freq, int dur) {
    if (s_muted) return;
    tone(BUZZER_PIN, freq, dur);
    delay(dur + 50);
}

static void fyCaw(int startFreq, int endFreq, int durationMs, int warbleHz) {
    if (s_muted) return;
    int steps = durationMs / 8;
    if (steps < 1) steps = 1;
    float fStep = (float)(endFreq - startFreq) / (float)steps;
    for (int i = 0; i < steps; i++) {
        int f = startFreq + (int)(fStep * i);
        if (warbleHz > 0 && (i % 3 == 0)) {
            f += ((i % 6 < 3) ? warbleHz : -warbleHz);
        }
        if (f < 100) f = 100;
        tone(BUZZER_PIN, f, 10);
        delay(8);
    }
    noTone(BUZZER_PIN);
}

#endif

void fyAudioInit() {
#if defined(FY_USE_I2S_AUDIO)
    fyI2sInit();
#else
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
#endif
}

void fyAudioSetMuted(bool muted) { s_muted = muted; }
bool fyAudioIsMuted() { return s_muted; }

void fyBootBeep() {
    if (s_muted) return;
    fyCaw(850, 380, 180, 40);
    delay(100);
    fyCaw(780, 350, 150, 50);
    delay(100);
    fyCaw(820, 280, 220, 60);
    delay(80);
    fyBeep(600, 25);
    delay(40);
    fyBeep(550, 25);
}

void fyDetectBeep() {
    if (s_muted) return;
    fyCaw(400, 900, 100, 30);
    delay(60);
    fyCaw(450, 950, 100, 30);
    delay(60);
    fyCaw(900, 350, 200, 50);
}

void fyHeartbeat() {
    if (s_muted) return;
    fyCaw(500, 400, 80, 20);
    delay(120);
    fyCaw(480, 380, 80, 20);
}
