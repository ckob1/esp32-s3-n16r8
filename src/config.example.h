/**
 * ============================================================
 *  config.example.h - 配置模板
 *  复制为 src/config.h 后填写自己的 API Key / WiFi 默认值
 * ============================================================
 */
#pragma once
#include <Arduino.h>
#include "logger.h"

// ----- TFT 显示 -----
#define TFT_BL_PIN        -1
#define TFT_ROTATION      1

// ----- 触摸 XPT2046 -----
#define TOUCH_CS_PIN      5
#define TOUCH_IRQ_PIN     4
#define TOUCH_SCK_PIN     6
#define TOUCH_MOSI_PIN    7
#define TOUCH_MISO_PIN    9

// 正式版固化校准参数
#define TOUCH_X_MIN       362
#define TOUCH_X_MAX       3914
#define TOUCH_Y_MIN       150
#define TOUCH_Y_MAX       3722
#define TOUCH_SWAP_XY     true
#define TOUCH_INVERT_X    true
#define TOUCH_INVERT_Y    true

// ----- I2S 音频输出 -----
#define I2S_OUT_PORT      I2S_NUM_0
#define I2S_OUT_BCLK      18
#define I2S_OUT_LRC       8
#define I2S_OUT_DIN       17
#define I2S_OUT_SR        16000

// ----- I2S 麦克风输入 -----
#define I2S_IN_PORT       I2S_NUM_1
#define I2S_IN_SCK        41
#define I2S_IN_WS         42
#define I2S_IN_SD         40
#define I2S_IN_SR         16000
#define I2S_IN_BITS       32

// ----- 板载 RGB LED -----
#define RGB_LED_PIN       48

// ----- WiFi 默认值 (生产环境留空, 用配网写入 NVS) -----
#define WIFI_SSID         ""
#define WIFI_PWD          ""
#define WIFI_TIMEOUT_MS   20000

// ----- LLM 供应商配置 -----
#define LLM_DEFAULT_PROVIDER  5

#define LLM_ZAI_NAME        "Z.ai"
#define LLM_ZAI_ENDPOINT    "https://api.z.ai/api/paas/v4/chat/completions"
#define LLM_ZAI_KEY         ""
#define LLM_ZAI_MODEL       "glm-4-flash"

#define LLM_GLM_CN_NAME     "GLM-CN"
#define LLM_GLM_CN_ENDPOINT "https://open.bigmodel.cn/api/paas/v4/chat/completions"
#define LLM_GLM_CN_KEY      ""
#define LLM_GLM_CN_MODEL    "glm-4-flash"

#define LLM_DS_NAME         "DeepSeek"
#define LLM_DS_ENDPOINT     "https://api.deepseek.com/v1/chat/completions"
#define LLM_DS_KEY          ""
#define LLM_DS_MODEL        "deepseek-chat"

#define LLM_KS_NAME         "Moonshot"
#define LLM_KS_ENDPOINT     "https://api.moonshot.cn/v1/chat/completions"
#define LLM_KS_KEY          ""
#define LLM_KS_MODEL        "moonshot-v1-8k"

#define LLM_OAI_NAME        "OpenAI"
#define LLM_OAI_ENDPOINT    "https://api.openai.com/v1/chat/completions"
#define LLM_OAI_KEY         ""
#define LLM_OAI_MODEL       "gpt-4o-mini"

#define LLM_AGNES_NAME      "Agnes"
#define LLM_AGNES_ENDPOINT  "https://apihub.agnes-ai.cn/v1/chat/completions"
#define LLM_AGNES_KEY       ""
#define LLM_AGNES_MODEL     "agnes-2.5-flash"

#define LLM_TIMEOUT_MS    30000
#define LLM_MAX_TOKENS    1024
#define LLM_TEMPERATURE   0.7

#define LLM_SYSTEM_PROMPT "You are an AI assistant on an ESP32-S3 device. Answer briefly in English, keep replies under 150 words, and use plain text without Markdown."

// ----- UI 颜色 -----
#define UI_COLOR_BG       ILI9341_BLACK
#define UI_COLOR_TITLE    ILI9341_CYAN
#define UI_COLOR_TEXT     ILI9341_WHITE
#define UI_COLOR_BTN_BG   ILI9341_NAVY
#define UI_COLOR_BTN_FG   ILI9341_WHITE
#define UI_COLOR_BTN_HOT  ILI9341_DARKCYAN
#define UI_COLOR_ERR      ILI9341_RED
#define UI_COLOR_OK       ILI9341_GREEN
#define UI_COLOR_WARN     ILI9341_YELLOW

#define DEBUG_SERIAL      1
#if DEBUG_SERIAL
  #define DBG_PRINT(x)    logger_print(x)
  #define DBG_PRINTLN(x)  logger_println(x)
#else
  #define DBG_PRINT(x)
  #define DBG_PRINTLN(x)
#endif
