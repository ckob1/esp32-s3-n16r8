/**
 * cn_font.h - 中文字体渲染 (基于 Adafruit GFX + U8g2 字体数据)
 *
 *  方案: 直接用 u8g2_font_unifont_t_chinese 字体数组
 *  U8g2 的 unifont_t_chinese 字体数据是公开的位图格式
 *  我们手动解析位图, 用 Adafruit GFX 的 drawPixel 绘制
 *
 *  替代之前的 U8g2 实例化方案 (有 API 兼容性问题)
 */
#pragma once
#include <Adafruit_ILI9341.h>
#include <Adafruit_GFX.h>

// 初始化字体 (无需复杂初始化, 字体数据是静态的)
void cn_font_init();

// 在 TFT 上绘制一个字符 (支持中文, 自动处理 UTF-8 多字节)
// 返回字符宽度 (像素)
int cn_draw_char(int x, int y, uint32_t codepoint, uint16_t color, uint16_t bg, uint8_t size);

// 在 TFT 上绘制字符串 (UTF-8, 支持中英混合, 自动换行)
// 返回总绘制行数
int cn_draw_string(int x, int y, const String& s, uint16_t color, uint16_t bg, uint8_t size, int max_x);

// 获取字符串在指定字号下的总宽度 (像素)
int cn_string_width(const String& s, uint8_t size);

// 获取指定字号下的字符高度 (像素)
int cn_char_height(uint8_t size);
