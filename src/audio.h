/**
 * audio.h - I2S 音频输出 (蜂鸣 / 未来 TTS)
 */
#pragma once
#include "config.h"

void audio_init();
void audio_beep(int frequency, int durationMs);
void audio_boot_melody();
void audio_ok_sound();
void audio_error_sound();
void audio_touch_sound();

// 麦克风 (I2S RX) + 扬声器播放
bool audio_mic_init();
size_t audio_mic_record(uint32_t ms, int16_t*& samples);
bool audio_play_pcm(const int16_t* samples, size_t count);
