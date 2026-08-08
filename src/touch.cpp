/**
 * touch.cpp - XPT2046 触摸实现 (独立 HSPI, 自带驱动)
 *
 *  自写 XPT2046 SPI 驱动, 不依赖任何外部触摸库
 *  协议: 8bit 命令 + 1bit busy + 12bit 数据 (MSB first)
 *
 *  命令字节 (12-bit, 差分模式, 内部参考关断):
 *    0xD0 = 读 X 通道 (在屏幕上是横向坐标的物理 ADC)
 *    0x90 = 读 Y 通道
 *    0xB0 = 读 Z1 (压力高字节)
 *    0xC0 = 读 Z2 (压力低字节)
 *
 *  ESP32-S3 用 HSPI 外设, 5 根杜邦线直连, 与 TFT 完全分离
 */
#include "touch.h"
#include "touch_calib.h"
#include "display.h"
#include <SPI.h>

static SPIClass touchSPI(HSPI);

// 保存最近一次触摸的原始值, 供动态校准使用
static RawPoint g_lastRaw = {0, 0, false};

RawPoint touch_get_last_raw() {
    return g_lastRaw;
}

// XPT2046 命令字节 (12-bit 模式, 差分, PD=00 即关断)
#define XPT_CMD_X   0xD0
#define XPT_CMD_Y   0x90
#define XPT_CMD_Z1  0xB0

// 触摸压力阈值 (Z1 原始值), 低于此值认为无触摸
#define TOUCH_Z_THRESHOLD  100

// ----- 内部: 读取一次 XPT2046 ADC -----
static uint16_t xpt_read(uint8_t cmd) {
    touchSPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(TOUCH_CS_PIN, LOW);

    touchSPI.transfer(cmd);
    // 读 16 bit (12bit 数据 + 4bit padding), 数据是 MSB first
    uint8_t hi = touchSPI.transfer(0x00);
    uint8_t lo = touchSPI.transfer(0x00);

    digitalWrite(TOUCH_CS_PIN, HIGH);
    touchSPI.endTransaction();

    // 12-bit 有效值, 数据格式: [busy 1bit][data 12bit MSB→LSB][padding 3bit]
    // 但实际 XPT2046 在 SPI_MODE0 下, 命令字节发完后下一个时钟就开始输出
    // hi 的 bit7=busy(忽略), bit6-3 = data[11-8], bit2-0 = data[7-5]
    // lo 的 bit7-3 = data[4-0], bit2-0 = padding
    // 所以: data = ((hi & 0x7F) << 5) | (lo >> 3)
    uint16_t val = ((uint16_t)(hi & 0x7F) << 5) | (lo >> 3);
    return val;
}

// 多次采样取平均, 滤掉抖动
static uint16_t xpt_read_avg(uint8_t cmd, int n = 3) {
    uint32_t sum = 0;
    int valid = 0;
    for (int i = 0; i < n; i++) {
        uint16_t v = xpt_read(cmd);
        sum += v;
        valid++;
    }
    return valid ? (sum / valid) : 0;
}

void touch_init() {
    DBG_PRINTLN("[Touch] init XPT2046 (HSPI)...");
    DBG_PRINTLN("[Touch] pins: CS=" + String(TOUCH_CS_PIN) +
                " IRQ=" + String(TOUCH_IRQ_PIN) +
                " SCK=" + String(TOUCH_SCK_PIN) +
                " MOSI=" + String(TOUCH_MOSI_PIN) +
                " MISO=" + String(TOUCH_MISO_PIN));

    pinMode(TOUCH_CS_PIN, OUTPUT);
    digitalWrite(TOUCH_CS_PIN, HIGH);  // 默认不选中

    if (TOUCH_IRQ_PIN >= 0) {
        pinMode(TOUCH_IRQ_PIN, INPUT_PULLUP);
    }

    // 初始化独立 SPI (HSPI)
    touchSPI.begin(TOUCH_SCK_PIN, TOUCH_MISO_PIN, TOUCH_MOSI_PIN, TOUCH_CS_PIN);

    // 自检: 读一次 Z1, 看是否返回合理值 (无触摸时 Z1 通常为 0 或很小)
    uint16_t z = xpt_read(XPT_CMD_Z1);
    DBG_PRINTLN("[Touch] self test Z1=" + String(z) + " (idle ~0)");
    DBG_PRINTLN("[Touch] init done");
}

// 把原始 ADC 值映射到屏幕像素 (用 g_touchCalib 动态校准值)
static void raw_to_screen(uint16_t rx, uint16_t ry, int& sx, int& sy) {
    long x, y;

    // 防御: 校准值异常 (xMin==xMax) 时 map() 会除零, 强制一个安全差值
    int calXMin = g_touchCalib.xMin;
    int calXMax = g_touchCalib.xMax;
    int calYMin = g_touchCalib.yMin;
    int calYMax = g_touchCalib.yMax;
    if (calXMax <= calXMin) calXMax = calXMin + 1;
    if (calYMax <= calYMin) calYMax = calYMin + 1;

    if (g_touchCalib.swapXY) {
        // raw_y 对应屏幕 X, raw_x 对应屏幕 Y
        x = map(ry, calXMin, calXMax, 0, tft.width());
        y = map(rx, calYMin, calYMax, 0, tft.height());
    } else {
        x = map(rx, calXMin, calXMax, 0, tft.width());
        y = map(ry, calYMin, calYMax, 0, tft.height());
    }

    if (g_touchCalib.invertX) x = tft.width()  - x;
    if (g_touchCalib.invertY) y = tft.height() - y;

    if (x < 0) x = 0; else if (x > tft.width())  x = tft.width();
    if (y < 0) y = 0; else if (y > tft.height()) y = tft.height();

    sx = (int)x;
    sy = (int)y;
}

TouchPoint touch_read() {
    TouchPoint tp = { 0, 0, false };

    // IRQ 引脚: 低电平 = 有触摸 (XPT2046 PENIRQ 默认拉低)
    if (TOUCH_IRQ_PIN >= 0 && digitalRead(TOUCH_IRQ_PIN) == HIGH) {
        return tp;  // 无触摸, 快速返回
    }

    // 检测压力 (Z1)
    uint16_t z1 = xpt_read_avg(XPT_CMD_Z1, 3);
    if (z1 < TOUCH_Z_THRESHOLD) {
        return tp;  // 压力太小, 视为无触摸
    }

    // 读 X, Y
    uint16_t rx = xpt_read_avg(XPT_CMD_X, 3);
    uint16_t ry = xpt_read_avg(XPT_CMD_Y, 3);

    // 保存原始值, 供动态校准使用
    g_lastRaw.x = rx;
    g_lastRaw.y = ry;
    g_lastRaw.valid = true;

    raw_to_screen(rx, ry, tp.x, tp.y);
    tp.pressed = true;

    // 调试: 点击时同时输出 raw 和 mapped
    DBG_PRINTLN("[Touch] raw(" + String(rx) + "," + String(ry) +
                ") z=" + String(z1) +
                " → screen(" + String(tp.x) + "," + String(tp.y) + ")");

    return tp;
}

TouchPoint touch_wait_press(int timeoutMs) {
    uint32_t t0 = millis();
    while (timeoutMs <= 0 || (millis() - t0) < (uint32_t)timeoutMs) {
        TouchPoint tp = touch_read();
        if (tp.pressed) return tp;
        delay(10);
    }
    return { 0, 0, false };
}

void touch_wait_release() {
    // 等到 IRQ 持续高 (无触摸) 一段时间
    uint32_t stableSince = millis();
    while (millis() - stableSince < 30) {  // 至少稳定 30ms
        if (TOUCH_IRQ_PIN >= 0) {
            if (digitalRead(TOUCH_IRQ_PIN) == LOW) {
                stableSince = millis();  // 还在按下, 重置
            }
        } else {
            // 没有 IRQ 引脚, 用 Z1 判断
            uint16_t z1 = xpt_read(XPT_CMD_Z1);
            if (z1 >= TOUCH_Z_THRESHOLD) {
                stableSince = millis();
            }
        }
        delay(5);
    }
}

void touch_debug_raw() {
    // 不论 IRQ 状态都直接读, 用于校准时观察原始值
    uint16_t x = xpt_read(XPT_CMD_X);
    uint16_t y = xpt_read(XPT_CMD_Y);
    uint16_t z = xpt_read(XPT_CMD_Z1);
    bool irq = (TOUCH_IRQ_PIN >= 0) ? (digitalRead(TOUCH_IRQ_PIN) == LOW) : true;

    if (z >= TOUCH_Z_THRESHOLD || irq) {
        // 有触摸: 每次都打印
        Serial.printf("[Touch RAW] x=%4u y=%4u z=%4u  (PRESSED)\n", x, y, z);
    } else {
        // 无触摸: 每 1 秒打印一次基线
        static uint32_t lastPrint = 0;
        if (millis() - lastPrint > 1000) {
            Serial.printf("[Touch RAW] x=%4u y=%4u z=%4u  (idle)\n", x, y, z);
            lastPrint = millis();
        }
    }
}
