/**
 * audio.cpp - I2S 音频输出实现
 * 用 I2S_NUM_0 做蜂鸣 (正弦波合成), 未来可扩展 TTS PCM 播放
 */
#include "audio.h"
#include "driver/i2s.h"
#include "driver/gpio.h"
#include <esp_heap_caps.h>
#include "LittleFS.h"
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

static bool micReady = false;
static bool outReady = false;
static SemaphoreHandle_t i2sOutLock = NULL;
static volatile int g_volume = 80;
static volatile uint32_t g_i2sOutRate = I2S_OUT_SR;

static void audio_load_volume() {
    Preferences prefs;
    if (!prefs.begin("audio_cfg", true)) {
        prefs.end();
        return;
    }
    int v = prefs.getInt("vol", 80);
    prefs.end();
    if (v < 0 || v > 100) v = 80;
    g_volume = v;
}

void audio_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    g_volume = volume;

    Preferences prefs;
    if (prefs.begin("audio_cfg", false)) {
        prefs.putInt("vol", g_volume);
        prefs.end();
    }
    Serial.println("[Audio] volume " + String(g_volume));
}

int audio_get_volume() {
    return g_volume;
}

static int16_t audio_scale_sample(int16_t sample) {
    return (int16_t)(((int32_t)sample * g_volume) / 100);
}

void audio_init() {
    DBG_PRINTLN("[Audio] init I2S output...");
    audio_load_volume();

    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = I2S_OUT_SR,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 16,
        .dma_buf_len = 512,
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
        DBG_PRINTLN("[Audio] I2S driver install failed!");
        return;
    }
    i2sOutLock = xSemaphoreCreateMutex();
    i2s_set_pin(I2S_OUT_PORT, &pins);
    i2s_zero_dma_buffer(I2S_OUT_PORT);
    g_i2sOutRate = I2S_OUT_SR;
    gpio_set_drive_capability((gpio_num_t)I2S_OUT_BCLK, GPIO_DRIVE_CAP_3);
    gpio_set_drive_capability((gpio_num_t)I2S_OUT_LRC, GPIO_DRIVE_CAP_3);
    gpio_set_drive_capability((gpio_num_t)I2S_OUT_DIN, GPIO_DRIVE_CAP_3);
    outReady = true;
    DBG_PRINTLN("[Audio] init done");
}

bool audio_beep(int frequency, int durationMs, bool wait) {
    if (!outReady) {
        DBG_PRINTLN("[Audio] output not initialized");
        return false;
    }
    if (i2sOutLock && xSemaphoreTake(i2sOutLock, 0) != pdTRUE) {
        DBG_PRINTLN("[Audio] output busy");
        return false;
    }
    const int sampleRate = I2S_OUT_SR;
    const int amplitude = 8000 * g_volume / 100;
    int samples = sampleRate * durationMs / 1000;
    int16_t* buf = (int16_t*)heap_caps_malloc(samples * sizeof(int16_t),
                                              MALLOC_CAP_SPIRAM);
    if (!buf) {
        if (i2sOutLock) xSemaphoreGive(i2sOutLock);
        return false;
    }

    for (int i = 0; i < samples; i++) {
        float angle = 2.0f * PI * frequency * i / sampleRate;
        // 软起软落, 减少爆音
        float env = 1.0f;
        int fade = sampleRate / 50; // 20ms fade
        if (i < fade) env = (float)i / fade;
        else if (i > samples - fade) env = (float)(samples - i) / fade;
        buf[i] = (int16_t)(amplitude * env * sinf(angle));
    }

    int16_t* stereo = (int16_t*)heap_caps_malloc(samples * 2 * sizeof(int16_t),
                                                 MALLOC_CAP_SPIRAM);
    if (!stereo) {
        heap_caps_free(buf);
        if (i2sOutLock) xSemaphoreGive(i2sOutLock);
        return false;
    }
    for (int i = 0; i < samples; i++) {
        stereo[i * 2] = buf[i];
        stereo[i * 2 + 1] = buf[i];
    }
    size_t written = 0;
    bool ok = i2s_write(I2S_OUT_PORT, stereo, samples * 2 * sizeof(int16_t),
                        &written, pdMS_TO_TICKS(5000)) == ESP_OK &&
              written == (size_t)samples * 2 * sizeof(int16_t);
    heap_caps_free(stereo);
    heap_caps_free(buf);
    if (i2sOutLock) xSemaphoreGive(i2sOutLock);
    if (wait) delay(durationMs);
    return ok;
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
    audio_beep(3000, 30, false);
}

bool audio_mic_init() {
    DBG_PRINTLN("[Audio] init I2S mic input...");

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
        DBG_PRINTLN("[Audio] I2S mic driver install failed!");
        return false;
    }
    i2s_set_pin(I2S_IN_PORT, &pins);
    micReady = true;
    DBG_PRINTLN("[Audio] mic init done");
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

    int16_t peak = 0;
    for (size_t i = 0; i < readSamples; i++) {
        samples[i] = (int16_t)(raw[i] >> 14);
        int16_t v = samples[i] < 0 ? -samples[i] : samples[i];
        if (v > peak) peak = v;
    }
    heap_caps_free(raw);
    Serial.println("[Audio] mic recorded " + String(readSamples) +
                   " samples, peak " + String(peak));
    return readSamples;
}

bool audio_play_pcm(const int16_t* samples, size_t count) {
    return audio_play_pcm_interleaved(samples, count, 1, I2S_OUT_SR);
}

bool audio_play_pcm_interleaved(const int16_t* samples, size_t count, int channels, uint32_t sampleRate) {
    if (!outReady || !samples || count == 0 || (channels != 1 && channels != 2)) return false;
    if (i2sOutLock && xSemaphoreTake(i2sOutLock, 0) != pdTRUE) {
        DBG_PRINTLN("[Audio] output busy");
        return false;
    }

    const int16_t* src = samples;
    int16_t* owned = NULL;
    size_t writeBytes = 0;

    if (channels == 1) {
        owned = (int16_t*)heap_caps_malloc(count * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
        if (!owned) {
            if (i2sOutLock) xSemaphoreGive(i2sOutLock);
            return false;
        }
        for (size_t i = 0; i < count; i++) {
            int16_t v = audio_scale_sample(samples[i]);
            owned[i * 2] = v;
            owned[i * 2 + 1] = v;
        }
        src = owned;
        writeBytes = count * 2 * sizeof(int16_t);
    } else {
        // 立体声: 每声道 count 个采样, 交错后共 2*count 个 int16
        writeBytes = count * 2 * sizeof(int16_t);
        if (g_volume < 100) {
            owned = (int16_t*)heap_caps_malloc(count * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
            if (!owned) {
                if (i2sOutLock) xSemaphoreGive(i2sOutLock);
                return false;
            }
            for (size_t i = 0; i < count * 2; i++) {
                owned[i] = audio_scale_sample(samples[i]);
            }
            src = owned;
        }
    }

    if (g_i2sOutRate != sampleRate) {
        i2s_set_sample_rates(I2S_OUT_PORT, sampleRate);
        g_i2sOutRate = sampleRate;
    }
    size_t written = 0;
    bool ok = i2s_write(I2S_OUT_PORT, src, writeBytes, &written,
                        pdMS_TO_TICKS(5000)) == ESP_OK && written == writeBytes;
    if (owned) heap_caps_free(owned);
    if (i2sOutLock) xSemaphoreGive(i2sOutLock);
    return ok && written > 0;
}

void audio_reset_output() {
    if (!outReady) return;
    if (i2sOutLock && xSemaphoreTake(i2sOutLock, pdMS_TO_TICKS(200)) != pdTRUE) return;
    i2s_set_sample_rates(I2S_OUT_PORT, I2S_OUT_SR);
    g_i2sOutRate = I2S_OUT_SR;
    i2s_zero_dma_buffer(I2S_OUT_PORT);
    if (i2sOutLock) xSemaphoreGive(i2sOutLock);
}

void audio_build_wav_header(uint8_t* hdr, uint32_t dataBytes, uint32_t sampleRate) {
    if (!hdr) return;
    memset(hdr, 0, 44);
    memcpy(hdr, "RIFF", 4);
    uint32_t riffSize = 36 + dataBytes;
    memcpy(hdr + 4, &riffSize, 4);
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    uint32_t fmtSize = 16;
    uint16_t audioFmt = 1;
    uint16_t channels = 1;
    uint16_t bits = 16;
    memcpy(hdr + 16, &fmtSize, 4);
    memcpy(hdr + 20, &audioFmt, 2);
    memcpy(hdr + 22, &channels, 2);
    memcpy(hdr + 24, &sampleRate, 4);
    uint32_t byteRate = sampleRate * channels * 2;
    uint16_t blockAlign = channels * 2;
    memcpy(hdr + 28, &byteRate, 4);
    memcpy(hdr + 32, &blockAlign, 2);
    memcpy(hdr + 34, &bits, 2);
    memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &dataBytes, 4);
}

bool audio_play_wav_file(const char* path, volatile bool* stopRequested) {
    if (!outReady || !path) return false;

    if (!LittleFS.begin(false)) {
        return false;
    }

    File f = LittleFS.open(path, "r");
    if (!f) {
        return false;
    }

    uint16_t channels = 1;
    uint16_t bits = 16;
    uint32_t sampleRate = 16000;
    uint32_t dataPos = 0;
    uint32_t dataSize = 0;

    char riff[5] = {0};
    if (f.read((uint8_t*)riff, 4) != 4) {
        f.close();
        return false;
    }
    uint32_t riffSize = 0;
    if (f.read((uint8_t*)&riffSize, 4) != 4) {
        f.close();
        return false;
    }
    char wave[5] = {0};
    if (f.read((uint8_t*)wave, 4) != 4) {
        f.close();
        return false;
    }
    if (memcmp(riff, "RIFF", 4) != 0 || memcmp(wave, "WAVE", 4) != 0) {
        Serial.println("[Music] not a RIFF/WAVE file");
        f.close();
        return false;
    }

    while (f.position() < 256 && f.available() >= 8) {
        char id[5] = {0};
        if (f.read((uint8_t*)id, 4) != 4) break;
        uint32_t sz = 0;
        if (f.read((uint8_t*)&sz, 4) != 4) break;

        if (memcmp(id, "fmt ", 4) == 0) {
            uint8_t fmtBuf[64];
            int fr = f.read(fmtBuf, min((uint32_t)sizeof(fmtBuf), sz));
            if (fr >= 16) {
                uint16_t audioFormat = 0;
                memcpy(&audioFormat, fmtBuf, 2);
                memcpy(&channels, fmtBuf + 2, 2);
                memcpy(&sampleRate, fmtBuf + 4, 4);
                memcpy(&bits, fmtBuf + 14, 2);
                if (audioFormat != 1) {
                    f.close();
                    return false;
                }
            }
            if (sz > (uint32_t)fr) f.seek(sz - fr, SeekCur);
        } else if (memcmp(id, "data", 4) == 0) {
            dataPos = f.position();
            dataSize = sz;
            break;
        } else {
            f.seek(sz + (sz & 1), SeekCur);
            // LIST/INFO 这类带子块 padding 的元数据块可能多一个 0 填充字节
            if (f.peek() == 0) f.seek(1, SeekCur);
        }
    }

    if (dataSize == 0 || dataPos == 0 || (channels != 1 && channels != 2) || bits != 16) {
        Serial.println("[Music] wav header invalid: rate=" + String(sampleRate) +
                       " ch=" + String(channels) + " bits=" + String(bits) +
                       " dataPos=" + String(dataPos) + " dataSize=" + String(dataSize));
        f.close();
        return false;
    }
    if (i2sOutLock && xSemaphoreTake(i2sOutLock, portMAX_DELAY) != pdTRUE) {
        f.close();
        return false;
    }

    if (g_i2sOutRate != sampleRate) {
        i2s_set_sample_rates(I2S_OUT_PORT, sampleRate);
        g_i2sOutRate = sampleRate;
    }
    f.seek(dataPos, SeekSet);

    uint8_t* raw = (uint8_t*)heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
    int16_t* stereo = (int16_t*)heap_caps_malloc(1024 * 2 * sizeof(int16_t),
                                                 MALLOC_CAP_SPIRAM);
    if (!raw || !stereo) {
        if (raw) heap_caps_free(raw);
        if (stereo) heap_caps_free(stereo);
        f.close();
        if (i2sOutLock) xSemaphoreGive(i2sOutLock);
        return false;
    }

    uint32_t left = dataSize;
    bool ok = true;
    bool stopped = false;

    while (left > 0 && ok && !(stopRequested && *stopRequested)) {
        int want = left > 1024 ? 1024 : (int)left;
        int got = f.read(raw, want);
        if (got <= 0) break;

        int n = got / 2;
        for (int i = 0; i < n; i++) {
            int16_t sample = 0;
            memcpy(&sample, raw + i * 2, 2);
            sample = audio_scale_sample(sample);
            if (channels == 2) {
                stereo[i] = sample;
            } else {
                stereo[i * 2] = sample;
                stereo[i * 2 + 1] = sample;
            }
        }

        size_t frameBytes = (channels == 2)
            ? n * sizeof(int16_t)
            : n * 2 * sizeof(int16_t);
        size_t written = 0;
        esp_err_t err = i2s_write(I2S_OUT_PORT, stereo, frameBytes, &written,
                                  pdMS_TO_TICKS(2000));
        if (err != ESP_OK || written < frameBytes) {
            ok = false;
            break;
        }
        left -= (channels == 2) ? (uint32_t)(n * 2) : (uint32_t)(n * 2);
    }

    stopped = stopRequested && *stopRequested;
    if (g_i2sOutRate != I2S_OUT_SR) {
        i2s_set_sample_rates(I2S_OUT_PORT, I2S_OUT_SR);
        g_i2sOutRate = I2S_OUT_SR;
    }
    i2s_zero_dma_buffer(I2S_OUT_PORT);

    heap_caps_free(raw);
    heap_caps_free(stereo);
    f.close();
    if (i2sOutLock) xSemaphoreGive(i2sOutLock);

    return ok && !stopped;
}
