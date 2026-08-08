#include "voice.h"
#include "config.h"
#include "audio.h"
#include "wifi_utils.h"
#include "web_server.h"
#include "ble_provision.h"
#include "cert_bundle.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

static int16_t* g_recordPcm = NULL;
static size_t g_recordCount = 0;

static void tls_client_setup(WiFiClientSecure& client) {
    client.setCACertBundle(x509_crt_bundle);
    client.setHandshakeTimeout(15000);
}

static String http_post_form(const String& url, const String& body) {
    WiFiClientSecure client;
    tls_client_setup(client);
    HTTPClient http;
    if (!http.begin(client, url)) return "HTTP begin failed";
    http.setConnectTimeout(10000);
    http.setTimeout(12000);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int code = http.POST(body);
    String out;
    if (code == 200) out = http.getString();
    else out = "HTTP " + String(code) + " " + http.errorToString(code);
    http.end();
    return out;
}

static String http_post_json(const String& url, const String& body) {
    WiFiClientSecure client;
    tls_client_setup(client);
    HTTPClient http;
    if (!http.begin(client, url)) return "HTTP begin failed";
    http.setConnectTimeout(10000);
    http.setTimeout(12000);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    String out;
    if (code == 200) out = http.getString();
    else out = "HTTP " + String(code) + " " + http.errorToString(code);
    http.end();
    return out;
}

static String http_get(const String& url) {
    WiFiClientSecure client;
    tls_client_setup(client);
    HTTPClient http;
    if (!http.begin(client, url)) return "HTTP begin failed";
    http.setConnectTimeout(10000);
    http.setTimeout(12000);
    int code = http.GET();
    String out;
    if (code == 200) out = http.getString();
    else out = "HTTP " + String(code) + " " + http.errorToString(code);
    http.end();
    return out;
}

static String json_escape(const String& s) {
    String out;
    for (unsigned i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if ((unsigned char)c < 0x20) {
            char tmp[8];
            snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned char)c);
            out += tmp;
        } else {
            out += c;
        }
    }
    return out;
}

static String voice_err(const String& msg) {
    return "VOICE_ERR:" + msg;
}

static String voice_put_file(const String& url, const uint8_t* data,
                             size_t len, String& etag) {
    WiFiClient plain;
    WiFiClientSecure secure;
    WiFiClient* client = &plain;
    if (url.startsWith("https://")) {
        tls_client_setup(secure);
        client = &secure;
    }
    HTTPClient http;
    if (!http.begin(*client, url)) return "HTTP begin failed";
    http.setConnectTimeout(10000);
    http.setTimeout(20000);
    http.addHeader("User-Agent", "Mozilla/5.0");
    const char* etagKeys[] = {"Etag", "etag"};
    http.collectHeaders(etagKeys, 2);
    int code = http.sendRequest("PUT", const_cast<uint8_t*>(data), len);
    String out;
    if (code == 200) {
        out = http.getString();
        etag = http.header("Etag");
        if (etag.length() == 0) etag = http.header("etag");
    } else {
        out = "HTTP " + String(code) + " " + http.errorToString(code);
    }
    http.end();
    return out;
}

static String voice_start_chat_inner() {
    if (!wifi_is_connected() && wifi_has_saved_credentials()) {
        Serial.println("[Voice] reconnecting WiFi...");
        if (wifi_connect()) {
            web_server_stop();
            ble_provision_stop();
        }
    }
    if (!wifi_is_connected()) return voice_err("WiFi not connected");

    // 分配额外 44 字节用于 WAV 头部
    size_t byteCount = (size_t)I2S_IN_SR * VOICE_RECORD_MS / 1000;
    uint8_t* pcm = (uint8_t*)heap_caps_malloc(byteCount * sizeof(int16_t) + 44,
                                              MALLOC_CAP_SPIRAM);
    if (!pcm) return voice_err("OOM record");
    int16_t* samples = NULL;
    size_t count = 0;
    count = audio_mic_record(VOICE_RECORD_MS, samples);
    if (!samples || count == 0) {
        if (samples) heap_caps_free(samples);
        heap_caps_free(pcm);
        return voice_err("mic record failed");
    }
    memcpy(pcm + 44, samples, count * sizeof(int16_t));
    heap_caps_free(samples);

    uint32_t audioBytes = count * sizeof(int16_t);
    uint32_t total = 44 + audioBytes;
    audio_build_wav_header(pcm, audioBytes, I2S_IN_SR);

    String etag;
    String createBody = "type=2&name=voice.wav&size=" + String(total) +
                        "&resource_file_type=wav&model_id=" +
                        String(VOICE_ASR_MODEL_ID);
    String body = http_post_form(
        String(VOICE_ASR_BASE) + "/resource/create", createBody);
    JsonDocument createDoc;
    DeserializationError de = deserializeJson(createDoc, body);
    if (de || createDoc["code"].as<int>() != 0) {
        heap_caps_free(pcm);
        return voice_err("upload failed " + body.substring(0, 160));
    }

    String resourceId = createDoc["data"]["resource_id"].as<String>();
    String inBossKey = createDoc["data"]["in_boss_key"].as<String>();
    String uploadId = createDoc["data"]["upload_id"].as<String>();
    const char* upUrl = createDoc["data"]["upload_urls"][0] | "";
    if (resourceId.length() == 0 || inBossKey.length() == 0 ||
        uploadId.length() == 0 || strlen(upUrl) == 0) {
        heap_caps_free(pcm);
        return voice_err("upload response malformed");
    }

    etag = "";
    String putResp = voice_put_file(upUrl, pcm, total, etag);
    if (etag.length() == 0) {
        heap_caps_free(pcm);
        return voice_err("put failed " + putResp.substring(0, 120));
    }

    String commitBody = "in_boss_key=" + inBossKey +
                        "&resource_id=" + resourceId +
                        "&etags=" + etag +
                        "&upload_id=" + uploadId +
                        "&model_id=" + String(VOICE_ASR_MODEL_ID);
    String completeResp = http_post_form(
        String(VOICE_ASR_BASE) + "/resource/create/complete", commitBody);
    JsonDocument completeDoc;
    de = deserializeJson(completeDoc, completeResp);
    if (de || completeDoc["code"].as<int>() != 0) {
        heap_caps_free(pcm);
        return voice_err("commit failed " + completeResp.substring(0, 160));
    }
    String downloadUrl = completeDoc["data"]["download_url"].as<String>();
    if (downloadUrl.length() == 0) {
        heap_caps_free(pcm);
        return voice_err("commit response malformed");
    }

    String taskJson = "{\"resource\":\"" + json_escape(downloadUrl) +
                      "\",\"model_id\":\"" + String(VOICE_ASR_MODEL_ID) + "\"}";
    String taskResp = http_post_json(String(VOICE_ASR_BASE) + "/task", taskJson);
    JsonDocument taskDoc;
    de = deserializeJson(taskDoc, taskResp);
    if (de || taskDoc["code"].as<int>() != 0) {
        heap_caps_free(pcm);
        return voice_err("task failed " + taskResp.substring(0, 160));
    }
    String taskId = taskDoc["data"]["task_id"].as<String>();
    if (taskId.length() == 0) {
        heap_caps_free(pcm);
        return voice_err("task response malformed");
    }

    String result;
    uint32_t deadline = millis() + VOICE_ASR_TIMEOUT_MS;
    while (millis() < deadline) {
        delay(1200);
        String queryUrl = String(VOICE_ASR_BASE) +
                          "/task/result?model_id=" + String(VOICE_ASR_MODEL_ID) +
                          "&task_id=" + taskId;
        String queryResp = http_get(queryUrl);
        JsonDocument doc;
        de = deserializeJson(doc, queryResp);
        if (de || doc["code"].as<int>() != 0) continue;
        int state = doc["data"]["state"] | 0;
        if (state == 4) {
            result = doc["data"]["result"].as<String>();
            break;
        }
        if (state == 3) {
            heap_caps_free(pcm);
            return voice_err("ASR failed " +
                             doc["data"]["remark"].as<String>().substring(0, 80));
        }
    }
    heap_caps_free(pcm);
    if (result.length() == 0) return voice_err("ASR timeout");

    JsonDocument resultDoc;
    de = deserializeJson(resultDoc, result);
    if (de) return voice_err("ASR result parse failed");
    String transcript;
    JsonArray utterances = resultDoc["utterances"].as<JsonArray>();
    for (JsonObject u : utterances) {
        String t = u["transcript"].as<String>();
        t.trim();
        if (t.length() > 0) {
            if (transcript.length() > 0) transcript += " ";
            transcript += t;
        }
    }
    if (transcript.length() == 0) return voice_err("no speech detected");
    Serial.println("[Voice] ASR: " + transcript);
    return transcript;
}

String voice_start_chat() {
    if (g_recordPcm) return voice_err("recording busy");
    g_recordPcm = (int16_t*)1;
    String ret = voice_start_chat_inner();
    g_recordPcm = NULL;
    return ret;
}
