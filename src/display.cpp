/**
 * display.cpp - TFT 显示实现 (Adafruit ILI9341 + U8g2 中文)
 */
#include "display.h"
#include "ascii_font.h"
#include <SPI.h>

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

#define TEXT_TOP_Y      30
#define TEXT_BOTTOM_Y   (tft.height() - 20)
#define TEXT_LEFT_X     6
#define TEXT_LINE_H     10          // Adafruit GFX size=1 行高 (6x8 + 2 间距)
#define TEXT_SIZE       1

void display_init() {
    DBG_PRINTLN("[Display] init Adafruit ILI9341...");
    DBG_PRINTLN("[Display] pins: CS=" + String(TFT_CS_PIN) + " DC=" + String(TFT_DC_PIN) +
                " RST=" + String(TFT_RST_PIN) + " SCK=" + String(TFT_SCK_PIN) +
                " MOSI=" + String(TFT_MOSI_PIN));

    // 显式初始化 SPI (Adafruit 库不会自动 begin)
    DBG_PRINTLN("[Display] SPI.begin()...");
    SPI.begin(TFT_SCK_PIN, -1, TFT_MOSI_PIN, TFT_CS_PIN);
    delay(50);

    DBG_PRINTLN("[Display] tft.begin()...");
    tft.begin();
    delay(10);

    tft.setRotation(TFT_ROTATION);
    DBG_PRINTLN("[Display] rotation=" + String(TFT_ROTATION) +
                " size=" + String(tft.width()) + "x" + String(tft.height()));

    tft.fillScreen(ILI9341_BLACK);
    DBG_PRINTLN("[Display] TFT init done");

    // 初始化中文字体
    cn_font_init();
}

void display_clear(const String& title) {
    tft.fillScreen(ILI9341_BLACK);

    // 顶部标题栏 (用 tft 原生, 标题英文居多)
    tft.fillRect(0, 0, tft.width(), 24, ILI9341_DARKCYAN);

    // 标题文字 (支持中文)
    display_draw_text(4, 4, title, ILI9341_WHITE, 2);

    // 底部状态栏分割线
    tft.drawFastHLine(0, tft.height() - 18, tft.width(), ILI9341_DARKGREY);
}

// 单行文字绘制 (支持中文)
void display_draw_text(int x, int y, const String& text, uint16_t color, uint8_t size) {
    cn_draw_string(x, y, text, color, ILI9341_BLACK, size, tft.width() - 4);
}

// 自动换行文字 (支持中文, 限制最大宽度)
void display_draw_text_wrap(int x, int y, int max_w, const String& text, uint16_t color, uint8_t size) {
    cn_draw_string(x, y, text, color, ILI9341_BLACK, size, x + max_w);
}

// 主显示区打印 (自动换行, 截断超长)
void display_print(const String& text, uint16_t color) {
    int lineH = cn_char_height(TEXT_SIZE) + 2;
    int max_w = tft.width() - TEXT_LEFT_X - 4;
    int maxLines = (TEXT_BOTTOM_Y - TEXT_TOP_Y) / lineH;

    int cx = TEXT_LEFT_X;
    int cy = TEXT_TOP_Y;
    int line = 1;

    int pos = 0;
    while (pos < text.length() && line <= maxLines) {
        // 解码一个 UTF-8 字符
        uint32_t cp = 0;
        uint8_t c = (uint8_t)text[pos];
        int adv = 1;
        if (c < 0x80) cp = c;
        else if ((c & 0xE0) == 0xC0 && pos + 1 < text.length()) {
            cp = ((uint32_t)(c & 0x1F) << 6) | ((uint8_t)text[pos+1] & 0x3F);
            adv = 2;
        } else if ((c & 0xF0) == 0xE0 && pos + 2 < text.length()) {
            cp = ((uint32_t)(c & 0x0F) << 12) |
                 ((uint32_t)(text[pos+1] & 0x3F) << 6) |
                 ((uint8_t)text[pos+2] & 0x3F);
            adv = 3;
        } else if ((c & 0xF8) == 0xF0 && pos + 3 < text.length()) {
            cp = ((uint32_t)(c & 0x07) << 18) |
                 ((uint32_t)(text[pos+1] & 0x3F) << 12) |
                 ((uint32_t)(text[pos+2] & 0x3F) << 6) |
                 ((uint8_t)(text[pos+3]) & 0x3F);
            adv = 4;
        }

        // 换行
        if (cp == '\n') {
            cx = TEXT_LEFT_X;
            cy += lineH;
            line++;
            pos += adv;
            continue;
        }

        // 计算字符宽度, 判断是否换行
        char utf8buf[5] = {0};
        for (int i = 0; i < adv; i++) utf8buf[i] = text[pos + i];
        String singleChar = String(utf8buf);
        int cw = cn_string_width(singleChar, TEXT_SIZE);

        if (cx + cw > TEXT_LEFT_X + max_w) {
            cx = TEXT_LEFT_X;
            cy += lineH;
            line++;
            if (line > maxLines) break;
        }

        cn_draw_char(cx, cy, cp, color, ILI9341_BLACK, TEXT_SIZE);
        cx += cw;
        pos += adv;
    }

    if (pos < text.length()) {
        display_draw_text(4, tft.height() - 30, "...(truncated)", ILI9341_YELLOW, 1);
    }
}

void display_status(const String& text, uint16_t color) {
    tft.fillRect(0, tft.height() - 16, tft.width(), 16, ILI9341_DARKGREY);
    display_draw_text(4, tft.height() - 14, text, color, 1);
}

Button display_draw_button(int x, int y, int w, int h, const String& label, bool pressed) {
    uint16_t bg = pressed ? ILI9341_DARKCYAN : ILI9341_NAVY;
    uint16_t border = pressed ? ILI9341_GREEN : ILI9341_CYAN;

    tft.fillRect(x, y, w, h, bg);
    tft.drawRect(x, y, w, h, border);

    // 居中显示 (支持中文)
    int tw = cn_string_width(label, 2);
    int th = cn_char_height(2);
    int cx = x + (w - tw) / 2;
    int cy = y + (h - th) / 2;
    if (cx < x + 2) cx = x + 2;
    if (cy < y + 2) cy = y + 2;
    cn_draw_string(cx, cy, label, ILI9341_WHITE, bg, 2, x + w - 2);

    return { x, y, w, h, label };
}

bool display_point_in_button(const Button& b, int x, int y) {
    return (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h);
}

void display_error(const String& msg) {
    display_clear("ERROR");
    display_draw_text(10, 50, "ERROR:", ILI9341_RED, 2);
    display_print(msg, ILI9341_RED);
    display_status("Error", ILI9341_RED);
}

void display_ok(const String& title, const String& body) {
    display_clear(title);
    display_print(body, ILI9341_GREEN);
    display_status("OK", ILI9341_GREEN);
}

void display_splash() {
    tft.fillScreen(ILI9341_BLACK);

    display_draw_text(20, 60, "ESP32-S3", ILI9341_CYAN, 3);
    display_draw_text(30, 100, "AI Assistant", ILI9341_WHITE, 2);
    display_draw_text(40, 140, "ILI9341 + Touch + AI", ILI9341_YELLOW, 1);
    display_draw_text(50, 160, "Booting...", ILI9341_GREEN, 1);
}

void display_self_test() {
    DBG_PRINTLN("[Display] === screen self test ===");
    DBG_PRINTLN("[Display] TFT_ROTATION=" + String(TFT_ROTATION));

    int w = tft.width();
    int h = tft.height();
    DBG_PRINTLN("[Display] resolution: " + String(w) + "x" + String(h));

    tft.fillScreen(ILI9341_RED);
    DBG_PRINTLN("[Display] stage 1: red (3s)");
    delay(3000);

    tft.fillScreen(ILI9341_GREEN);
    DBG_PRINTLN("[Display] stage 2: green (3s)");
    delay(3000);

    tft.fillScreen(ILI9341_BLUE);
    DBG_PRINTLN("[Display] stage 3: blue (3s)");
    delay(3000);

    // 阶段 4: 中文测试
    tft.fillScreen(ILI9341_BLACK);
    DBG_PRINTLN("[Display] stage 4: font test (5s)");

    display_draw_text(2, 2, "=== TFT Self Test ===", ILI9341_WHITE, 1);
    display_draw_text(2, 18, "Size: " + String(w) + "x" + String(h), ILI9341_WHITE, 1);
    display_draw_text(2, 30, "Rotation: " + String(TFT_ROTATION), ILI9341_WHITE, 1);

    display_draw_text(2, 50, "Font test (ASCII):", ILI9341_YELLOW, 2);
    display_draw_text(2, 80, "Hello, World!", ILI9341_GREEN, 2);
    display_draw_text(2, 110, "ESP32-S3 AI Assistant", ILI9341_CYAN, 1);
    display_draw_text(2, 130, "LCD + Touch + WiFi", ILI9341_WHITE, 1);

    display_draw_text(2, 160, "UI is English to avoid", ILI9341_WHITE, 1);
    display_draw_text(2, 175, "missing Chinese font boxes", ILI9341_WHITE, 1);

    display_draw_text(2, 200, "Wait 5s -> boot...", ILI9341_GREEN, 1);

    delay(5000);
    DBG_PRINTLN("[Display] === self test done ===\n");
    tft.fillScreen(ILI9341_BLACK);
}
