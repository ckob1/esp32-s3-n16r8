#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "driver/i2s.h"  // ESP32内置I2S库，无第三方依赖

// ===== 音频引脚配置（和之前的接线完全兼容，不用改线）=====
#define I2S_DIN_PIN  17  // MAX98357A DIN → GPIO17
#define I2S_BCLK_PIN 18  // MAX98357A BCLK → GPIO18
#define I2S_LRC_PIN  8   // MAX98357A LRC/WS → GPIO8

// WiFi & AI配置（完全保留原有配置，无改动）
const char* WIFI_SSID     = "OnePlus-Ace-5-m4sg";
const char* WIFI_PWD      = "88888888";
const char* LM_STUDIO_IP  = "192.168.45.160";
const int   LM_STUDIO_PORT = 1234;
const char* LM_MODEL_NAME = "qwen/qwen3-v1-30b";

String aiResponse = "";
bool psramAvailable = false;

// ===== 内置I2S音频工具：播放提示音 =====
void playBeep(int frequency, int durationMs) {
  const int sampleRate = 16000;
  const int amplitude = 8000;
  int samples = sampleRate * durationMs / 1000;
  int16_t* buffer = (int16_t*)malloc(samples * sizeof(int16_t));

  // 生成正弦波
  for (int i = 0; i < samples; i++) {
    float angle = 2 * PI * frequency * i / sampleRate;
    buffer[i] = (int16_t)(amplitude * sin(angle));
  }

  // 播放音频
  size_t bytesWritten;
  i2s_write(I2S_NUM_0, buffer, samples * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
  free(buffer);
  delay(durationMs);
}

// ===== 初始化MAX98357A（内置I2S，无第三方库）=====
void initAudioModule() {
  Serial.println("\n========== 初始化MAX98357A音频模块 ==========");
  
  // I2S配置，完全匹配MAX98357A
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

  // 引脚绑定
  i2s_pin_config_t pinConfig = {
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRC_PIN,
    .data_out_num = I2S_DIN_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  // 初始化I2S
  esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2sConfig, 0, NULL);
  if (err != ESP_OK) {
    Serial.println("❌ I2S驱动安装失败");
    return;
  }
  err = i2s_set_pin(I2S_NUM_0, &pinConfig);
  if (err != ESP_OK) {
    Serial.println("❌ I2S引脚配置失败");
    return;
  }

  Serial.println("✅ MAX98357A 初始化完成");
  // 开机测试音，接好线就能听到
  playBeep(1500, 200);
  playBeep(2000, 200);
}

// WiFi连接（完全保留原有逻辑，加了音频提示）
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
    playBeep(2500, 300); // WiFi成功提示音
  } else {
    Serial.println("\n❌ WiFi连接失败");
    playBeep(500, 1000); // 失败提示音
    while (1) delay(1000);
  }
}

// AI请求函数（完全保留原有逻辑，加了音频反馈）
bool sendToLMStudio(String prompt) {
  String apiUrl = "http://" + String(LM_STUDIO_IP) + ":" + String(LM_STUDIO_PORT) + "/v1/chat/completions";
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(60000);

  if (!http.begin(apiUrl)) {
    Serial.println("❌ HTTP初始化失败");
    return false;
  }
  http.addHeader("Content-Type", "application/json");

  // 利用PSRAM扩展缓冲区
  const size_t reqBufSize = psramAvailable ? 8192 : 2048;
  const size_t respBufSize = psramAvailable ? 16384 : 4096;
  DynamicJsonDocument reqJson(reqBufSize);
  DynamicJsonDocument respJson(respBufSize);

  // 构建标准OpenAI格式请求体
  reqJson["model"] = LM_MODEL_NAME;
  reqJson["messages"][0]["role"] = "user";
  reqJson["messages"][0]["content"] = prompt;
  reqJson["temperature"] = 0.7;
  reqJson["max_tokens"] = 1024;

  String reqBody;
  serializeJson(reqJson, reqBody);
  Serial.println("\n📤 发送请求：" + prompt);

  // 发送请求
  int httpCode = http.POST(reqBody);
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DeserializationError err = deserializeJson(respJson, payload);
    
    if (!err) {
      aiResponse = respJson["choices"][0]["message"]["content"].as<String>();
      Serial.println("✅ AI回复：\n" + aiResponse);
      
      // AI回复成功，播放提示音+简易语音播报
      playBeep(1800, 200);
      // 【已修复min函数报错】统一参数类型
      for (int i = 0; i < min((int)aiResponse.length(), 60); i++) {
        int freq = 800 + (aiResponse[i] % 15) * 150;
        playBeep(freq, 70);
        delay(10);
      }
    } else {
      aiResponse = "❌ JSON解析失败: " + String(err.c_str());
      Serial.println(aiResponse);
      playBeep(600, 500);
    }
  } else {
    aiResponse = "❌ 请求失败，HTTP错误码: " + String(httpCode);
    Serial.println(aiResponse);
    playBeep(500, 500);
  }

  http.end();
  return httpCode == HTTP_CODE_OK;
}

// PSRAM检测（完全保留原有逻辑）
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
    // 大内存测试，验证八线PSRAM
    char* testBuf = (char*)ps_malloc(6 * 1024 * 1024);
    if (testBuf) {
      Serial.println("✅ 6MB内存分配成功（八线PSRAM正常工作）");
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

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=====================================");
  Serial.println("    ESP32-S3 AI助手（无依赖音频版）    ");
  Serial.println("=====================================");
  Serial.print("Flash容量：");
  Serial.print(ESP.getFlashChipSize() / 1024 / 1024);
  Serial.println(" MB");
  
  initAudioModule();  // 优先初始化音频
  checkPSRAM();
  connectWiFi();
  sendToLMStudio("你好，用一句话介绍下自己");
}

void loop() {
  // 串口输入交互
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
