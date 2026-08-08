/**
 * audio.h - I2S 音频输出 (蜂鸣 / 未来 TTS)
 */
#pragma once
#include "config.h"

void audio_init();
bool audio_beep(int frequency, int durationMs, bool wait = true);
void audio_boot_melody();
void audio_ok_sound();
void audio_error_sound();
void audio_touch_sound();
void audio_set_volume(int volume);
int audio_get_volume();

// 麦克风 (I2S RX) + 扬声器播放
bool audio_mic_init();
size_t audio_mic_record(uint32_t ms, int16_t*& samples);
bool audio_play_pcm(const int16_t* samples, size_t count);
bool audio_play_pcm_interleaved(const int16_t* samples, size_t count, int channels, uint32_t sampleRate);
void audio_reset_output();
bool audio_play_wav_file(const char* path, volatile bool* stopRequested = NULL);

// 构建 44 字节标准单声道 16bit PCM WAV 头 (RIFF size 修正为 36+dataBytes)
void audio_build_wav_header(uint8_t* hdr44, uint32_t dataBytes, uint32_t sampleRate);
