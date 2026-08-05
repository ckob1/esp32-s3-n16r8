/**
 * touch_calib.h - 动态触摸校准 (NVS 持久化)
 *
 *  把校准值存到 ESP32 的 NVS (Non-Volatile Storage)
 *  下次开机自动读取, 无需重新校准
 *
 *  校准流程:
 *    1. 串口输入 "calib" 进入校准模式
 *    2. 屏幕显示 4 个角的红十字, 依次点击
 *    3. 程序自动算出校准参数, 存到 NVS
 *    4. 下次开机自动应用
 */
#pragma once
#include "config.h"

// 校准参数 (运行时使用, 启动时从 NVS 读取)
struct TouchCalib {
    int xMin, xMax;
    int yMin, yMax;
    bool swapXY;
    bool invertX;
    bool invertY;
    bool valid;     // NVS 里有没有有效校准
};

extern TouchCalib g_touchCalib;

// 校准算法版本; 升级算法后旧 NVS 校准会被忽略
#define TOUCH_CALIB_VERSION 2

// 初始化: 从 NVS 加载校准值, 没有就用 config.h 的默认值
void touch_calib_load();

// 保存新校准值到 NVS
void touch_calib_save(int xMin, int xMax, int yMin, int yMax,
                      bool swapXY, bool invertX, bool invertY);

// 清除 NVS 中的校准值, 恢复默认
void touch_calib_reset();

// 交互式 4 点校准
// 屏幕显示 4 个角的红十字, 用户依次点击
// 自动算出校准参数并保存
void touch_calib_run_interactive();
