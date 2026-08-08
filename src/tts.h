#pragma once
#include "config.h"

bool tts_speak(const String& text);
bool tts_is_busy();
void tts_stop();
