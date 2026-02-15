#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "driver/i2s.h"
#include <TFT_eSPI.h>  // ILI9341库

TFT_eSPI tft = TFT_eSPI();  // 屏幕对象

// ===== 音频引脚（原样）=====
#define I2S_DIN_PIN  17
#define I2S_BCLK_PIN 18
#define I2S_LRC_PIN  8

// WiFi & AI
const char* WIFI_SSID     = "OnePlus-Ace-5-m4sg";
const char* WIFI_PWD      = "88888888";
const char* LM_STUDIO_IP  = "192.168.45.160";
const int   LM_STUDIO_PORT = 1234;
const char* LM_MODEL_NAME = "qwen/qwen3-v1-30b";

String aiResponse = "";
bool psramAvailable = false;

// ===== 蜂鸣播放 =====
void playBeep(int frequency, int durationMs) {
  const int sampleRate = 16000;
  const int amplitude = 8000;
  int samples = sampleRate * durationMs / 1000;
  int16_t* buffer = (int16_t*)malloc(samples * sizeof(int16_t));

  for (int i = 0; i < samples; i++) {
    float angle = 2 * PI * frequency * i / sampleRate;
    buffer[i] = (int16_t)(amplitude * sin(angle));
  }

  size_t bytesWritten;
  i2s_write(I2S_NUM_0, buffer, samples * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
  free(buffer);
  delay(durationMs);
}

// ===== 音频初始化 =====
void initAudioModule() {
  Serial.println("\n初始化音频模块");

  i2s_config_t i2sConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pinConfig = {
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRC_PIN,
    .data_out_num = I2S_DIN_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &i2sConfig, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pinConfig);

  Serial.println("音频初始化完成");
  playBeep(1500, 200);
  playBeep(2000, 200);
}

// ===== PSRAM检测 =====
void checkPSRAM() {
  Serial.println("\n========== PSRAM 八线模式检测 ==========");
  Serial.println("配置：Octal SPI（8线，OPI模式）");
  Serial.print("芯片型号：");
  Serial.println(ESP.getChipModel());

  psramAvailable = psramFound();
  
  Serial.print("PSRAM状态：");
  Serial.println(psramAvailable ? "✅ 检测到" : "❌ 未检测到");
  Serial.print("总容量：");
  Serial.print(ESP.getPsramSize() / 1024 / 1024);
  Serial.println(" MB");
  Serial.print("可用容量：");
  Serial.print(ESP.getFreePsram() / 1024 / 1024);
  Serial.println(" MB");

  if (psramAvailable) {
    char* testBuf = (char*)ps_malloc(6 * 1024 * 1024);
    if (testBuf) {
      Serial.println("✅ 6MB内存分配成功");
      memset(testBuf, 0xAA, 6 * 1024 * 1024);
      Serial.println("✅ 6MB写入测试通过");
      free(testBuf);
      playBeep(2200, 200);
    } else {
      Serial.println("❌ 大内存分配失败");
      psramAvailable = false;
    }
  }
  Serial.println("==========================================");
}

// ===== 屏幕显示函数（优化换行，支持完整显示AI文字） =====
void displayText(String title, String content) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println(title);

  tft.setTextSize(1);  // 小字体，每字符约8像素宽，240宽屏幕约30字符/行
  tft.setCursor(10, 40);  // 从标题下开始，留间距

  int pos = 0;
  int lineHeight = 10;  // 每行高度（字体高度+间距）
  int maxLines = (tft.height() - 50) / lineHeight;  // 计算最大行数

  int lineCount = 0;
  while (pos < content.length() && lineCount < maxLines) {
    // 每行最多30字符，避免溢出
    int endPos = min(pos + 30, (int)content.length());
    // 智能换行：找空格或标点，避免切断单词
    while (endPos > pos && content[endPos - 1] != ' ' && content[endPos - 1] != ',' && content[endPos - 1] != '.') {
      endPos--;
    }
    if (endPos == pos) endPos = min(pos + 30, (int)content.length());  // 如果无空格，强制切

    String line = content.substring(pos, endPos);
    tft.println(line);
    pos = endPos;
    while (pos < content.length() && content[pos] == ' ') pos++;  // 跳过空格
    lineCount++;
  }

  if (pos < content.length()) {
    // 如果内容太长，显示提示
    tft.setCursor(10, tft.getCursorY() + 10);
    tft.setTextColor(TFT_YELLOW);
    tft.println("... (内容过长，查看串口完整回复)");
    Serial.println("屏幕显示不全，完整AI回复：" + content);
  }
}

// ===== AI请求 =====
bool sendToLMStudio(String prompt) {
  String apiUrl = "http://" + String(LM_STUDIO_IP) + ":" + String(LM_STUDIO_PORT) + "/v1/chat/completions";
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(60000);

  if (!http.begin(apiUrl)) {
    Serial.println("HTTP初始化失败");
    return false;
  }
  http.addHeader("Content-Type", "application/json");

  const size_t bufSize = psramAvailable ? 16384 : 4096;
  JsonDocument reqJson;
  JsonDocument respJson;

  reqJson["model"] = LM_MODEL_NAME;
  reqJson["messages"][0]["role"] = "user";
  reqJson["messages"][0]["content"] = prompt;
  reqJson["temperature"] = 0.7;
  reqJson["max_tokens"] = 1024;

  String reqBody;
  serializeJson(reqJson, reqBody);
  Serial.println("\n📤 发送请求：" + prompt);

  int httpCode = http.POST(reqBody);
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DeserializationError err = deserializeJson(respJson, payload);
    if (!err) {
      aiResponse = respJson["choices"][0]["message"]["content"].as<String>();
      Serial.println("✅ AI回复：\n" + aiResponse);
      displayText("输入: " + prompt, aiResponse);  // 屏幕显示优化后函数
      playBeep(1800, 200);
      return true;
    } else {
      Serial.println("JSON解析失败: " + String(err.c_str()));
    }
  } else {
    Serial.println("请求失败，HTTP码: " + String(httpCode));
  }
  http.end();
  displayText("错误", "请求失败 " + String(httpCode));
  playBeep(500, 500);
  return false;
}

// WiFi连接
void connectWiFi() {
  Serial.print("正在连接WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PWD);

  int retryCnt = 0;
  while (WiFi.status() != WL_CONNECTED && retryCnt < 20) {
    delay(500);
    Serial.print(".");
    retryCnt++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi连接成功！IP: " + WiFi.localIP().toString());
    displayText("WiFi", "连接成功\nIP: " + WiFi.localIP().toString());
    playBeep(2500, 300);
  } else {
    Serial.println("\n❌ WiFi连接失败");
    displayText("WiFi", "连接失败");
    playBeep(500, 1000);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=====================================");
  Serial.println("    ESP32-S3 AI助手（屏幕版）    ");
  Serial.println("=====================================");

  tft.init();
  tft.invertDisplay(false);  // false或true，如果颜色反转改true

  // 扩展测试：试多个rotation，找到正确的固定它
  for (int rot = 0; rot < 4; rot++) {
    tft.setRotation(rot);
    tft.fillScreen(TFT_RED);  // 红色填充验证
    delay(1000);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("Test OK! Rot: " + String(rot));
    delay(3000);  // 观察3秒
  }

  // 测试后固定一个rotation（根据观察改此值，例如3）
  tft.setRotation(1);  // 替换为测试中正确的rot值

  initAudioModule();
  checkPSRAM();
  connectWiFi();

  sendToLMStudio("你好，用一句话介绍下自己");  // 开机测试
}

void loop() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      playBeep(2000, 100);
      sendToLMStudio(input);
    }
  }
  delay(50);
}