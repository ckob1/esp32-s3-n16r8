/**
 * audio.cpp - I2S 音频输出实现
 * 用 I2S_NUM_0 做蜂鸣 (正弦波合成), 未来可扩展 TTS PCM 播放
 */
#include "audio.h"
#include "driver/i2s.h"
#include <esp_heap_caps.h>

static bool micReady = false;

void audio_init() {
    DBG_PRINTLN("[Audio] 初始化 I2S 输出...");

    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = I2S_OUT_SR,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 1024,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pins = {
        .bck_io_num = I2S_OUT_BCLK,
        .ws_io_num = I2S_OUT_LRC,
        .data_out_num = I2S_OUT_DIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    if (i2s_driver_install(I2S_OUT_PORT, &cfg, 0, NULL) != ESP_OK) {
        DBG_PRINTLN("[Audio] I2S 驱动安装失败!");
        return;
    }
    i2s_set_pin(I2S_OUT_PORT, &pins);
    DBG_PRINTLN("[Audio] 初始化完成");
}

void audio_beep(int frequency, int durationMs) {
    const int sampleRate = I2S_OUT_SR;
    const int amplitude = 8000;
    int samples = sampleRate * durationMs / 1000;
    int16_t* buf = (int16_t*)malloc(samples * sizeof(int16_t));
    if (!buf) return;

    for (int i = 0; i < samples; i++) {
        float angle = 2.0f * PI * frequency * i / sampleRate;
        // 软起软落, 减少爆音
        float env = 1.0f;
        int fade = sampleRate / 50; // 20ms fade
        if (i < fade) env = (float)i / fade;
        else if (i > samples - fade) env = (float)(samples - i) / fade;
        buf[i] = (int16_t)(amplitude * env * sinf(angle));
    }

    size_t written;
    i2s_write(I2S_OUT_PORT, buf, samples * sizeof(int16_t), &written, portMAX_DELAY);
    free(buf);
    delay(durationMs);
}

void audio_boot_melody() {
    audio_beep(1500, 150);
    delay(80);
    audio_beep(2000, 150);
    delay(80);
    audio_beep(2500, 200);
}

void audio_ok_sound() {
    audio_beep(2200, 100);
    delay(50);
    audio_beep(2600, 100);
}

void audio_error_sound() {
    audio_beep(500, 200);
    delay(100);
    audio_beep(400, 300);
}

void audio_touch_sound() {
    audio_beep(3000, 30);
}

bool audio_mic_init() {
    DBG_PRINTLN("[Audio] 初始化 I2S 麦克风输入...");

    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = I2S_IN_SR,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 1024,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pins = {
        .bck_io_num = I2S_IN_SCK,
        .ws_io_num = I2S_IN_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_IN_SD
    };

    if (i2s_driver_install(I2S_IN_PORT, &cfg, 0, NULL) != ESP_OK) {
        DBG_PRINTLN("[Audio] I2S 麦克风驱动安装失败!");
        return false;
    }
    i2s_set_pin(I2S_IN_PORT, &pins);
    micReady = true;
    DBG_PRINTLN("[Audio] 麦克风初始化完成");
    return true;
}

size_t audio_mic_record(uint32_t ms, int16_t*& samples) {
    samples = NULL;
    if (!micReady) return 0;

    size_t sampleCount = (size_t)I2S_IN_SR * ms / 1000;
    size_t bytesToRead = sampleCount * sizeof(int32_t);
    int32_t* raw = (int32_t*)heap_caps_malloc(bytesToRead, MALLOC_CAP_SPIRAM);
    if (!raw) return 0;

    size_t bytesRead = 0;
    if (i2s_read(I2S_IN_PORT, raw, bytesToRead, &bytesRead, portMAX_DELAY) != ESP_OK) {
        heap_caps_free(raw);
        return 0;
    }

    size_t readSamples = bytesRead / sizeof(int32_t);
    samples = (int16_t*)heap_caps_malloc(readSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!samples) {
        heap_caps_free(raw);
        return 0;
    }

    for (size_t i = 0; i < readSamples; i++) {
        samples[i] = (int16_t)(raw[i] >> 14);
    }
    heap_caps_free(raw);
    return readSamples;
}

bool audio_play_pcm(const int16_t* samples, size_t count) {
    if (!samples || count == 0) return false;

    size_t written = 0;
    if (i2s_write(I2S_OUT_PORT, samples, count * sizeof(int16_t), &written, portMAX_DELAY) != ESP_OK) {
        return false;
    }
    return written > 0;
}
