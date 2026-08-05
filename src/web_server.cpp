#include "web_server.h"
#include "wifi_utils.h"
#include "llm.h"
#include "audio.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <string.h>

static WebServer server(80);
static bool serverRunning = false;

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 AI OS</title>
<style>
body{font-family:sans-serif;max-width:520px;margin:24px auto;padding:0 12px;background:#f4f6f8;color:#222}
input{width:100%;padding:10px;margin:6px 0;box-sizing:border-box;font-size:16px}
button{width:100%;padding:12px;margin:8px 0;font-size:16px;background:#155e75;color:#fff;border:0;border-radius:6px}
.box{background:#fff;border:1px solid #d8dee4;padding:16px;border-radius:8px;margin:12px 0}
code{background:#eef2f6;padding:2px 5px;border-radius:4px}
</style>
</head>
<body>
<h1>ESP32 AI OS</h1>
<div class="box">
<b>Mode:</b> <span id="mode">...</span><br>
<b>IP:</b> <span id="ip">...</span><br>
<b>Provider:</b> <span id="provider">...</span>
</div>
<div class="box">
<h2>WiFi Setup</h2>
<form method="POST" action="/wifi">
<input name="ssid" placeholder="SSID" required>
<input name="pass" type="password" placeholder="Password">
<button>Save and Reboot</button>
</form>
</div>
<div class="box">
<h2>API</h2>
<p><code>GET /api/status</code></p>
<p><code>GET /api/chat?q=hello</code></p>
<p><code>POST /api/wifi</code></p>
</div>
<script>
fetch('/api/status').then(r=>r.json()).then(j=>{
document.getElementById('mode').textContent=j.mode;
document.getElementById('ip').textContent=j.ip;
document.getElementById('provider').textContent=j.provider;
});
</script>
</body>
</html>
)rawliteral";

static void handle_root() {
    server.send_P(200, "text/html", INDEX_HTML);
}

static void handle_status() {
    JsonDocument doc;
    doc["mode"] = wifi_is_ap_mode() ? "AP" : "STA";
    doc["ip"] = wifi_get_ip_display();
    doc["ssid"] = wifi_is_ap_mode() ? "ESP32-AI-Setup" : WiFi.SSID();
    doc["provider"] = llm_get_provider_name();
    doc["psram_kb"] = ESP.getPsramSize() / 1024;
    doc["heap"] = ESP.getFreeHeap();

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handle_wifi_post() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    ssid.trim();
    pass.trim();

    if (ssid.length() == 0) {
        server.send(400, "text/plain", "SSID is required");
        return;
    }

    wifi_save_credentials(ssid, pass);
    server.send(200, "text/plain", "Saved. Rebooting...");
    delay(500);
    ESP.restart();
}

static void handle_chat() {
    String q = server.arg("q");
    q.trim();

    JsonDocument doc;
    if (q.length() == 0) {
        doc["ok"] = false;
        doc["error"] = "Missing q parameter";
        String out;
        serializeJson(doc, out);
        server.send(400, "application/json", out);
        return;
    }

    LLMResult r = llm_chat(q);
    doc["ok"] = r.ok;
    doc["reply"] = r.content;
    doc["error"] = r.errorMsg;
    doc["http"] = r.httpCode;

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handle_reboot() {
    server.send(200, "text/plain", "Rebooting...");
    delay(300);
    ESP.restart();
}

static void handle_record() {
    int ms = server.arg("ms").toInt();
    if (ms <= 0 || ms > 10000) ms = 3000;

    int16_t* samples = NULL;
    size_t count = audio_mic_record(ms, samples);
    if (count == 0) {
        server.send(500, "text/plain", "Mic unavailable");
        return;
    }

    size_t dataLen = count * sizeof(int16_t);
    size_t wavLen = 44 + dataLen;
    uint8_t* wav = (uint8_t*)heap_caps_malloc(wavLen, MALLOC_CAP_SPIRAM);
    if (!wav) {
        heap_caps_free(samples);
        server.send(500, "text/plain", "No memory");
        return;
    }

    uint32_t sampleRate = I2S_IN_SR;
    memcpy(wav, "RIFF", 4);
    uint32_t riffSize = 36 + dataLen;
    memcpy(wav + 4, &riffSize, 4);
    memcpy(wav + 8, "WAVE", 4);
    memcpy(wav + 12, "fmt ", 4);
    uint32_t fmtSize = 16;
    uint16_t audioFmt = 1;
    uint16_t channels = 1;
    memcpy(wav + 16, &fmtSize, 4);
    memcpy(wav + 20, &audioFmt, 2);
    memcpy(wav + 22, &channels, 2);
    memcpy(wav + 24, &sampleRate, 4);
    uint32_t byteRate = sampleRate * channels * 2;
    uint16_t blockAlign = channels * 2;
    uint16_t bitsPerSample = 16;
    memcpy(wav + 28, &byteRate, 4);
    memcpy(wav + 32, &blockAlign, 2);
    memcpy(wav + 34, &bitsPerSample, 2);
    memcpy(wav + 36, "data", 4);
    memcpy(wav + 40, &dataLen, 4);
    memcpy(wav + 44, samples, dataLen);

    server.setContentLength(wavLen);
    server.send(200, "audio/wav", "");
    server.sendContent((const char*)wav, wavLen);
    heap_caps_free(samples);
    heap_caps_free(wav);
}

void web_server_start() {
    if (serverRunning) return;

    server.on("/", HTTP_GET, handle_root);
    server.on("/api/status", HTTP_GET, handle_status);
    server.on("/api/chat", HTTP_GET, handle_chat);
    server.on("/api/record", HTTP_GET, handle_record);
    server.on("/wifi", HTTP_POST, handle_wifi_post);
    server.on("/api/wifi", HTTP_POST, handle_wifi_post);
    server.on("/api/reboot", HTTP_GET, handle_reboot);
    server.begin();
    serverRunning = true;
    DBG_PRINTLN("[Web] HTTP server ready");
}

void web_server_stop() {
    if (!serverRunning) return;
    server.stop();
    serverRunning = false;
}

void web_server_handle() {
    if (serverRunning) {
        server.handleClient();
    }
}
