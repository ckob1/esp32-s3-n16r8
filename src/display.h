/**
 * display.h - TFT 显示封装 (Adafruit ILI9341 + U8g2 中文)
 */
#pragma once
#include "config.h"
#include <Adafruit_ILI9341.h>
#include <Adafruit_GFX.h>

extern Adafruit_ILI9341 tft;

void display_init();

void display_clear(const String& title);

// 显示文字 (支持中文, 自动换行)
void display_print(const String& text, uint16_t color = 0xFFFF);

// 在指定位置绘制单行文字 (支持中文, 不自动换行)
void display_draw_text(int x, int y, const String& text, uint16_t color, uint8_t size = 1);

// 在指定区域绘制文字 (支持中文, 自动换行, 超长截断)
void display_draw_text_wrap(int x, int y, int max_w, const String& text, uint16_t color, uint8_t size = 1);

void display_status(const String& text, uint16_t color = 0xFFE0);

struct Button { int x, y, w, h; String label; };
Button display_draw_button(int x, int y, int w, int h, const String& label, bool pressed = false);

bool display_point_in_button(const Button& b, int x, int y);

void display_error(const String& msg);

void display_ok(const String& title, const String& body);

void display_splash();

void display_self_test();
