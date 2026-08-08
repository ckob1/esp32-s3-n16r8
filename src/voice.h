#pragma once
#include <Arduino.h>

// 录音 -> 免费 Bcut ASR 语音识别
// 返回识别文本; 失败时返回以 "VOICE_ERR:" 开头的错误信息
String voice_start_chat();
