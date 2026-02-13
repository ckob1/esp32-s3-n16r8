#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* WIFI_SSID     = "OnePlus-Ace-5-m4sg";
const char* WIFI_PWD      = "88888888";
const char* LM_STUDIO_IP  = "192.168.45.160";
const int   LM_STUDIO_PORT = 1234;
const char* LM_MODEL_NAME = "qwen/qwen3-v1-30b";

String aiResponse = "";
bool psramAvailable = false;

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
  } else {
    Serial.println("\n❌ WiFi连接失败");
    while (1) delay(1000);
  }
}

bool sendToLMStudio(String prompt) {
  String apiUrl = "http://" + String(LM_STUDIO_IP) + ":" + String(LM_STUDIO_PORT) + "/v1/chat/completions";
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(60000);
  if (!http.begin(apiUrl)) return false;
  http.addHeader("Content-Type", "application/json");

  const size_t reqBufSize = psramAvailable ? 8192 : 2048;
  const size_t respBufSize = psramAvailable ? 16384 : 4096;
  DynamicJsonDocument reqJson(reqBufSize);
  DynamicJsonDocument respJson(respBufSize);

  reqJson["model"] = LM_MODEL_NAME;
  reqJson["messages"][0]["role"] = "user";
  reqJson["messages"][0]["content"] = prompt;
  reqJson["temperature"] = 0.7;
  reqJson["max_tokens"] = 1024;

  String reqBody;
  serializeJson(reqJson, reqBody);
  Serial.println("\n📤 发送请求：" + prompt);
  Serial.println("请求体：" + reqBody);
  
  int httpCode = http.POST(reqBody);
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("原始响应：" + payload.substring(0, 200) + "..."); // 加调试，打印部分响应
    DeserializationError err = deserializeJson(respJson, payload);
    if (!err) {
      aiResponse = respJson["choices"][0]["message"]["content"].as<String>();
      Serial.println("✅ AI回复：\n" + aiResponse);
    } else {
      aiResponse = "❌ JSON解析失败: " + String(err.c_str());
      Serial.println(aiResponse);
    }
  } else {
    aiResponse = "❌ 请求失败，HTTP: " + String(httpCode);
    Serial.println(aiResponse);
  }
  http.end();
  return httpCode == HTTP_CODE_OK;
}

// PSRAM检测 - 使用Arduino标准API
void checkPSRAM() {
  Serial.println("\n========== PSRAM 八线模式检测 ==========");
  Serial.println("配置：Octal SPI（8线，OPI模式）");
  Serial.print("芯片：");
  Serial.println(ESP.getChipModel());

  // 关键：使用 psramFound() 而不是 psramInit()
  psramAvailable = psramFound();
  
  Serial.print("PSRAM状态：");
  Serial.println(psramAvailable ? "✅ 检测到" : "❌ 未检测到");
  Serial.print("总容量：");
  Serial.print(ESP.getPsramSize() / 1024 / 1024);
  Serial.println(" MB");
  Serial.print("可用：");
  Serial.print(ESP.getFreePsram() / 1024 / 1024);
  Serial.println(" MB");

  if (psramAvailable) {
    // 大内存测试验证八线模式
    char* testBuf = (char*)ps_malloc(6 * 1024 * 1024);
    if (testBuf) {
      Serial.println("✅ 6MB分配成功（八线PSRAM正常工作）");
      memset(testBuf, 0xAA, 6 * 1024 * 1024);
      Serial.println("✅ 6MB写入测试通过");
      free(testBuf);
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
  Serial.println("    ESP32-S3 本地AI助手（PSRAM修复版）    ");
  Serial.println("=====================================");
  Serial.print("Flash：");
  Serial.print(ESP.getFlashChipSize() / 1024 / 1024);
  Serial.println(" MB");
  
  checkPSRAM();
  connectWiFi();
  sendToLMStudio("你好，用一句话介绍下自己");
}

void loop() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) sendToLMStudio(input);
  }
  delay(100);
}