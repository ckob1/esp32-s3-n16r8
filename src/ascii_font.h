/**
 * ascii_font.h - ASCII text renderer (Adafruit GFX built-in font)
 */
#pragma once
#include <Adafruit_ILI9341.h>
#include <Adafruit_GFX.h>

// Init text renderer
void cn_font_init();

// Draw one UTF-8 character; non-ASCII is drawn as a box.
int cn_draw_char(int x, int y, uint32_t codepoint, uint16_t color, uint16_t bg, uint8_t size);

// Draw UTF-8 string with wrapping.
int cn_draw_string(int x, int y, const String& s, uint16_t color, uint16_t bg, uint8_t size, int max_x);

// Get string width in pixels.
int cn_string_width(const String& s, uint8_t size);

// Get character height in pixels.
int cn_char_height(uint8_t size);
