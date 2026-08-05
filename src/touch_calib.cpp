/**
 * touch_calib.cpp - 动态触摸校准实现
 *
 *  用 ESP32 Preferences 库 (基于 NVS) 保存校准值
 *  用 4 点校准法: 屏幕显示 4 个角, 用户点击, 自动算出 min/max + 方向
 */
#include "touch_calib.h"
#include "display.h"
#include "touch.h"
#include "audio.h"
#include <Preferences.h>

TouchCalib g_touchCalib;

static const char* NVS_NAMESPACE = "touch_calib";

void touch_calib_load() {
    Preferences prefs;
    bool opened = prefs.begin(NVS_NAMESPACE, false);   // 读写模式, 首次自动建命名空间

    if (!opened) {
        prefs.end();
        g_touchCalib.xMin = TOUCH_X_MIN;
        g_touchCalib.xMax = TOUCH_X_MAX;
        g_touchCalib.yMin = TOUCH_Y_MIN;
        g_touchCalib.yMax = TOUCH_Y_MAX;
        g_touchCalib.swapXY = TOUCH_SWAP_XY;
        g_touchCalib.invertX = TOUCH_INVERT_X;
        g_touchCalib.invertY = TOUCH_INVERT_Y;
        g_touchCalib.valid = false;
        return;
    }

    int version = prefs.getInt("version", 0);
    if (version != TOUCH_CALIB_VERSION) {
        prefs.end();
        g_touchCalib.xMin = TOUCH_X_MIN;
        g_touchCalib.xMax = TOUCH_X_MAX;
        g_touchCalib.yMin = TOUCH_Y_MIN;
        g_touchCalib.yMax = TOUCH_Y_MAX;
        g_touchCalib.swapXY = TOUCH_SWAP_XY;
        g_touchCalib.invertX = TOUCH_INVERT_X;
        g_touchCalib.invertY = TOUCH_INVERT_Y;
        g_touchCalib.valid = false;
        DBG_PRINTLN("[TouchCalib] 旧校准版本已忽略, 请重新校准");
        return;
    }

    g_touchCalib.xMin    = prefs.getInt("xMin",    TOUCH_X_MIN);
    g_touchCalib.xMax    = prefs.getInt("xMax",    TOUCH_X_MAX);
    g_touchCalib.yMin    = prefs.getInt("yMin",    TOUCH_Y_MIN);
    g_touchCalib.yMax    = prefs.getInt("yMax",    TOUCH_Y_MAX);
    g_touchCalib.swapXY  = prefs.getBool("swapXY", TOUCH_SWAP_XY);
    g_touchCalib.invertX = prefs.getBool("invertX", TOUCH_INVERT_X);
    g_touchCalib.invertY = prefs.getBool("invertY", TOUCH_INVERT_Y);
    g_touchCalib.valid   = prefs.getBool("valid",   false);

    prefs.end();

    DBG_PRINTLN("[TouchCalib] 加载:");
    DBG_PRINTLN("  X: " + String(g_touchCalib.xMin) + " - " + String(g_touchCalib.xMax));
    DBG_PRINTLN("  Y: " + String(g_touchCalib.yMin) + " - " + String(g_touchCalib.yMax));
    DBG_PRINTLN("  swapXY=" + String(g_touchCalib.swapXY) +
                " invX=" + String(g_touchCalib.invertX) +
                " invY=" + String(g_touchCalib.invertY));
    DBG_PRINTLN("  来源: " + String(g_touchCalib.valid ? "NVS (已校准)" : "config.h (默认)"));
}

void touch_calib_save(int xMin, int xMax, int yMin, int yMax,
                      bool swapXY, bool invertX, bool invertY) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);  // 读写模式

    prefs.putInt("xMin",    xMin);
    prefs.putInt("xMax",    xMax);
    prefs.putInt("yMin",    yMin);
    prefs.putInt("yMax",    yMax);
    prefs.putBool("swapXY",  swapXY);
    prefs.putBool("invertX", invertX);
    prefs.putBool("invertY", invertY);
    prefs.putBool("valid",   true);
    prefs.putInt("version",  TOUCH_CALIB_VERSION);

    prefs.end();

    // 更新内存中的值
    g_touchCalib.xMin = xMin; g_touchCalib.xMax = xMax;
    g_touchCalib.yMin = yMin; g_touchCalib.yMax = yMax;
    g_touchCalib.swapXY = swapXY;
    g_touchCalib.invertX = invertX;
    g_touchCalib.invertY = invertY;
    g_touchCalib.valid = true;

    DBG_PRINTLN("[TouchCalib] ✅ 已保存到 NVS");
}

void touch_calib_reset() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();

    g_touchCalib.xMin = TOUCH_X_MIN;
    g_touchCalib.xMax = TOUCH_X_MAX;
    g_touchCalib.yMin = TOUCH_Y_MIN;
    g_touchCalib.yMax = TOUCH_Y_MAX;
    g_touchCalib.swapXY = TOUCH_SWAP_XY;
    g_touchCalib.invertX = TOUCH_INVERT_X;
    g_touchCalib.invertY = TOUCH_INVERT_Y;
    g_touchCalib.valid = false;

    DBG_PRINTLN("[TouchCalib] 已清除 NVS, 恢复 config.h 默认值");
}

// 等待一次有效点击, 返回 raw 坐标 (RawPoint 在 touch.h 已定义)
static RawPoint wait_for_raw_click(uint32_t timeoutMs = 15000) {
    uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
        TouchPoint tp = touch_read();
        if (tp.pressed) {
            audio_touch_sound();
            touch_wait_release();
            return touch_get_last_raw();
        }
        if (Serial.available()) {
            Serial.read();
            return {0, 0, false};
        }
        delay(10);
    }
    return {0, 0, false};
}

// 在屏幕上画一个十字 + 数字
static void draw_target(int x, int y, int idx, const String& label) {
    // 清屏
    tft.fillScreen(ILI9341_BLACK);

    // 标题
    display_draw_text(40, 10, "Touch Calibration", ILI9341_CYAN, 2);
    display_draw_text(20, 40, "Tap the red cross center", ILI9341_WHITE, 1);
    display_draw_text(20, 55, label, ILI9341_YELLOW, 1);

    // 画十字 (大一点, 容易点中)
    tft.drawLine(x - 20, y, x + 20, y, ILI9341_RED);
    tft.drawLine(x, y - 20, x, y + 20, ILI9341_RED);
    tft.drawCircle(x, y, 15, ILI9341_RED);

    // 显示编号
    char num[8];
    sprintf(num, "%d/4", idx);
    display_draw_text(x - 10, y + 25, num, ILI9341_YELLOW, 2);

    // 底部提示
    display_draw_text(20, tft.height() - 20, "Send any serial char to cancel", ILI9341_DARKGREY, 1);
}

void touch_calib_run_interactive() {
    Serial.println("\n========== 动态触摸校准 ==========");
    Serial.println("将依次在屏幕 4 个角显示红色十字");
    Serial.println("请用触摸笔或手指点击十字中心");
    Serial.println("发送任意串口字符可取消\n");

    RawPoint samples[4];
    int leftTargetX = 80;
    int rightTargetX = tft.width() - 80;
    int topTargetY = 60;
    int bottomTargetY = tft.height() - 60;
    int screenX[4] = {leftTargetX, rightTargetX, leftTargetX, rightTargetX};
    int screenY[4] = {topTargetY, topTargetY, bottomTargetY, bottomTargetY};
    String labels[4] = {
        "1. Box Top-Left",
        "2. Box Top-Right",
        "3. Box Bottom-Left",
        "4. Box Bottom-Right"
    };

    for (int i = 0; i < 4; i++) {
        draw_target(screenX[i], screenY[i], i + 1, labels[i]);
        Serial.println("等待点击: " + labels[i]);

        RawPoint p = wait_for_raw_click(30000);
        if (!p.valid) {
            Serial.println("校准取消或超时");
            return;
        }
        samples[i] = p;
        Serial.println("  raw_x=" + String(p.x) + " raw_y=" + String(p.y));
        delay(300);
    }

    // 计算校准参数
    // 4 个角的 screen 位置 (中央标准框的四个顶点)

    int tW = tft.width();
    int tH = tft.height();

    Serial.println("\n=== 校准分析 ===");
    Serial.println("TL: raw(" + String(samples[0].x) + "," + String(samples[0].y) + ") screen(" + String(leftTargetX) + "," + String(topTargetY) + ")");
    Serial.println("TR: raw(" + String(samples[1].x) + "," + String(samples[1].y) + ") screen(" + String(rightTargetX) + "," + String(topTargetY) + ")");
    Serial.println("BL: raw(" + String(samples[2].x) + "," + String(samples[2].y) + ") screen(" + String(leftTargetX) + "," + String(bottomTargetY) + ")");
    Serial.println("BR: raw(" + String(samples[3].x) + "," + String(samples[3].y) + ") screen(" + String(rightTargetX) + "," + String(bottomTargetY) + ")");

    // 判断 raw_x 到底是屏幕 X 还是屏幕 Y
    int tl_tr_dx = abs(samples[0].x - samples[1].x);
    int tl_bl_dx = abs(samples[0].x - samples[2].x);
    bool swapXY = (tl_bl_dx > tl_tr_dx);

    long leftRaw, rightRaw, topRaw, bottomRaw;
    if (swapXY) {
        leftRaw   = (samples[0].y + samples[2].y) / 2;
        rightRaw  = (samples[1].y + samples[3].y) / 2;
        topRaw    = (samples[0].x + samples[1].x) / 2;
        bottomRaw = (samples[2].x + samples[3].x) / 2;
    } else {
        leftRaw   = (samples[0].x + samples[2].x) / 2;
        rightRaw  = (samples[1].x + samples[3].x) / 2;
        topRaw    = (samples[0].y + samples[1].y) / 2;
        bottomRaw = (samples[2].y + samples[3].y) / 2;
    }

    // 用已知的十字屏幕坐标线性外推到屏幕 0 和最大坐标
    long rawAtX0 = leftRaw + (long)(0 - leftTargetX) * (rightRaw - leftRaw) / (rightTargetX - leftTargetX);
    long rawAtXW = leftRaw + (long)(tW - leftTargetX) * (rightRaw - leftRaw) / (rightTargetX - leftTargetX);
    long rawAtY0 = topRaw + (long)(0 - topTargetY) * (bottomRaw - topRaw) / (bottomTargetY - topTargetY);
    long rawAtYH = topRaw + (long)(tH - topTargetY) * (bottomRaw - topRaw) / (bottomTargetY - topTargetY);

    bool invertX = rawAtX0 > rawAtXW;
    bool invertY = rawAtY0 > rawAtYH;
    int xMin = min(rawAtX0, rawAtXW);
    int xMax = max(rawAtX0, rawAtXW);
    int yMin = min(rawAtY0, rawAtYH);
    int yMax = max(rawAtY0, rawAtYH);

    Serial.println("\n=== 计算结果 ===");
    Serial.println("X: " + String(xMin) + " - " + String(xMax));
    Serial.println("Y: " + String(yMin) + " - " + String(yMax));
    Serial.println("swapXY=" + String(swapXY) + " invertX=" + String(invertX) + " invertY=" + String(invertY));

    // 保存
    touch_calib_save(xMin, xMax, yMin, yMax, swapXY, invertX, invertY);

    // 提示完成
    tft.fillScreen(ILI9341_BLACK);
    display_draw_text(20, 60, "Calibration Complete!", ILI9341_GREEN, 2);
    display_draw_text(20, 100, "Saved to NVS", ILI9341_WHITE, 1);
    display_draw_text(20, 120, "Applied after reboot", ILI9341_WHITE, 1);
    audio_ok_sound();
    delay(2000);
}
