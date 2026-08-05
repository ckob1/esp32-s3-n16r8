/**
 * cn_font.cpp - 文字渲染 (用 Adafruit GFX 自带 glcdfont)
 *
 *  完全自包含, 零外部依赖
 *  - ASCII: 直接调用 Adafruit GFX 的 drawChar (用 glcdfont 5x7 字体)
 *  - 中文/其他 Unicode: 显示为方块 □ 占位
 *  - 完整中文内容走串口输出 (Serial Monitor 可见)
 *
 *  这个方案最稳, 不会编译失败
 */
#include "cn_font.h"
#include "config.h"
#include <Arduino.h>

extern Adafruit_ILI9341 tft;

// Adafruit GFX 的 drawChar 内部用 glcdfont 字体
// 我们直接用 tft.drawChar, size 参数控制字号

// 字符尺寸 (Adafruit GFX 默认字体: 5x7, 但实际绘制 6x8 含间距)
#define FONT_CHAR_W   6
#define FONT_CHAR_H   8

void cn_font_init() {
    DBG_PRINTLN("[CN_Font] 使用 Adafruit GFX 内置字体 (glcdfont)");
    DBG_PRINTLN("[CN_Font] ASCII 完整支持, 中文显示为 □, 完整中文见串口");
}

// UTF-8 解码
static uint32_t utf8_next(const String& s, int& pos) {
    if (pos >= s.length()) return 0;
    uint8_t c = (uint8_t)s[pos++];
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0) {
        if (pos >= s.length()) return c;
        uint32_t cp = ((uint32_t)(c & 0x1F) << 6) | ((uint8_t)s[pos] & 0x3F);
        pos++;
        return cp;
    }
    if ((c & 0xF0) == 0xE0) {
        if (pos + 1 >= s.length()) return c;
        uint32_t cp = ((uint32_t)(c & 0x0F) << 12) |
                      ((uint32_t)(s[pos] & 0x3F) << 6) |
                      ((uint8_t)(s[pos+1]) & 0x3F);
        pos += 2;
        return cp;
    }
    if ((c & 0xF8) == 0xF0) {
        if (pos + 2 >= s.length()) return c;
        uint32_t cp = ((uint32_t)(c & 0x07) << 18) |
                      ((uint32_t)(s[pos] & 0x3F) << 12) |
                      ((uint32_t)(s[pos+1] & 0x3F) << 6) |
                      ((uint8_t)(s[pos+2]) & 0x3F);
        pos += 3;
        return cp;
    }
    return c;
}

int cn_draw_char(int x, int y, uint32_t codepoint, uint16_t color, uint16_t bg, uint8_t size) {
    if (codepoint < 0x80 && codepoint >= 0x20) {
        // ASCII: 直接用 Adafruit GFX 的 drawChar
        tft.drawChar(x, y, (char)codepoint, color, bg, size);
        return FONT_CHAR_W * size;
    } else if (codepoint == '\n' || codepoint == '\r') {
        return 0;
    } else {
        // 中文/其他: 画方块占位 □ (U+25A1)
        int w = FONT_CHAR_W * size;
        int h = FONT_CHAR_H * size;
        tft.fillRect(x, y, w, h, bg);
        tft.drawRect(x, y, w, h, color);
        return w;
    }
}

int cn_draw_string(int x, int y, const String& s, uint16_t color, uint16_t bg, uint8_t size, int max_x) {
    int cx = x;
    int cy = y;
    int lineH = cn_char_height(size);
    int lines = 1;

    int pos = 0;
    while (pos < s.length()) {
        uint32_t cp = utf8_next(s, pos);

        if (cp == '\n') {
            cx = x;
            cy += lineH;
            lines++;
            continue;
        }

        int cw = FONT_CHAR_W * size;
        if (max_x > 0 && cx > x && cx + cw > max_x) {
            cx = x;
            cy += lineH;
            lines++;
        }

        cn_draw_char(cx, cy, cp, color, bg, size);
        cx += cw;
    }
    return lines;
}

int cn_string_width(const String& s, uint8_t size) {
    int pos = 0;
    int count = 0;
    while (pos < s.length()) {
        utf8_next(s, pos);
        count++;
    }
    return count * FONT_CHAR_W * size;
}

int cn_char_height(uint8_t size) {
    return FONT_CHAR_H * size;
}
