#pragma once
#include <Arduino.h>

void logger_print(const String& s);
void logger_println(const String& s);
String logger_get_recent();
void logger_clear();
