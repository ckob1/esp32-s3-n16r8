/**
 * touch.h - XPT2046 触摸处理 (独立 HSPI, 自写驱动)
 *
 *  自写 XPT2046 SPI 驱动, 不依赖任何外部触摸库
 *  5 根杜邦线直连: T_CS / T_IRQ / T_SCK / T_MOSI / T_MISO
 */
#pragma once
#include "config.h"
#include <Adafruit_ILI9341.h>
#include <Adafruit_GFX.h>

extern Adafruit_ILI9341 tft;   // 用 display.cpp 里的同一个 tft 对象

struct TouchPoint {
    int   x;        // 屏幕坐标 (已经过校准+方向变换)
    int   y;
    bool  pressed;  // 当前是否按下
};

void     touch_init();

// 读一次触摸点 (非阻塞)
TouchPoint touch_read();

// 等待一次有效点击 (阻塞, timeoutMs<=0 表示永久等待)
TouchPoint touch_wait_press(int timeoutMs = 0);

// 等待触摸释放
void     touch_wait_release();

// 输出原始 ADC 值到串口 (用于校准)
void     touch_debug_raw();

// 获取最近一次触摸的原始 ADC 值 (用于动态校准)
// 返回 {x, y, valid}, valid=false 表示还没有触摸过
struct RawPoint { int x, y; bool valid; };
RawPoint touch_get_last_raw();
