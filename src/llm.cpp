/**
 * llm.cpp - 云端 LLM API 实现 (多供应商)
 *
 *  OpenAI 兼容协议, 5 家供应商预置, 运行时切换
 *  用 PSRAM 缓冲大响应, 避免内存溢出
 */
#include "llm.h"
#include "wifi_utils.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// 供应商表 (与 config.h 的宏对应)
LLMProvider providers[LLM_PROVIDER_COUNT] = {
    { LLM_ZAI_NAME,    LLM_ZAI_ENDPOINT,    LLM_ZAI_KEY,    LLM_ZAI_MODEL    },
    { LLM_GLM_CN_NAME, LLM_GLM_CN_ENDPOINT, LLM_GLM_CN_KEY, LLM_GLM_CN_MODEL },
    { LLM_DS_NAME,     LLM_DS_ENDPOINT,     LLM_DS_KEY,     LLM_DS_MODEL     },
    { LLM_KS_NAME,     LLM_KS_ENDPOINT,     LLM_KS_KEY,     LLM_KS_MODEL     },
    { LLM_OAI_NAME,    LLM_OAI_ENDPOINT,    LLM_OAI_KEY,    LLM_OAI_MODEL    },
    { LLM_AGNES_NAME,  LLM_AGNES_ENDPOINT,  LLM_AGNES_KEY,  LLM_AGNES_MODEL  }
};
int currentProviderIdx = LLM_DEFAULT_PROVIDER;

String llm_get_provider_name() {
    if (currentProviderIdx < 0 || currentProviderIdx >= LLM_PROVIDER_COUNT) return "?";
    return providers[currentProviderIdx].name;
}

int llm_get_provider_idx() {
    return currentProviderIdx;
}

bool llm_set_provider(int idx) {
    if (idx < 0 || idx >= LLM_PROVIDER_COUNT) return false;
    currentProviderIdx = idx;
    DBG_PRINTLN("[LLM] 切换供应商: " + providers[idx].name +
                " (model=" + providers[idx].model + ")");
    return true;
}

bool llm_set_provider_by_name(const String& name) {
    String n = name;
    n.toLowerCase();
    for (int i = 0; i < LLM_PROVIDER_COUNT; i++) {
        String pn = providers[i].name;
        pn.toLowerCase();
        // 模糊匹配: 输入是名字前缀
        if (pn.startsWith(n) || pn.indexOf(n) >= 0 ||
            n.startsWith(pn) || n.indexOf(pn) >= 0) {
            return llm_set_provider(i);
        }
    }
    return false;
}

bool llm_current_provider_ready() {
    if (currentProviderIdx < 0 || currentProviderIdx >= LLM_PROVIDER_COUNT) return false;
    String key = providers[currentProviderIdx].apiKey;
    key.trim();
    return key.length() > 0;
}

bool llm_next_provider() {
    for (int i = 1; i <= LLM_PROVIDER_COUNT; i++) {
        int idx = (currentProviderIdx + i) % LLM_PROVIDER_COUNT;
        String key = providers[idx].apiKey;
        key.trim();
        if (key.length() > 0) {
            return llm_set_provider(idx);
        }
    }
    // 没有任何 key 配置, 也切一下让用户看到
    return llm_set_provider((currentProviderIdx + 1) % LLM_PROVIDER_COUNT);
}

static bool parse_https_url(const String& url, String& host, int& port, String& path) {
    if (!url.startsWith("https://")) return false;

    String rest = url.substring(8);
    int slash = rest.indexOf('/');
    String hostPort = (slash >= 0) ? rest.substring(0, slash) : rest;
    path = (slash >= 0) ? rest.substring(slash) : "/";

    int colon = hostPort.indexOf(':');
    host = (colon >= 0) ? hostPort.substring(0, colon) : hostPort;
    port = (colon >= 0) ? hostPort.substring(colon + 1).toInt() : 443;

    return host.length() > 0 && port > 0;
}

static bool read_header_line(WiFiClientSecure& client, String& out, uint32_t deadline) {
    out = "";

    while (out.length() < 4096) {
        while (client.available() > 0) {
            char c = (char)client.read();
            if (c < 0) break;
            out += c;
            if (c == '\n') return true;
        }
        if (!client.connected() && client.available() == 0) return false;
        if (millis() > deadline) return false;
        delay(2);
    }
    return false;
}

static String read_http_response(WiFiClientSecure& client, int& httpCode) {
    httpCode = -1;
    uint32_t deadline = millis() + LLM_TIMEOUT_MS;

    String statusLine;
    if (!read_header_line(client, statusLine, deadline)) return "";
    int sp = statusLine.indexOf(' ');
    httpCode = (sp >= 0) ? statusLine.substring(sp + 1).toInt() : 0;

    bool chunked = false;
    int contentLength = -1;
    while (true) {
        String line;
        if (!read_header_line(client, line, deadline)) break;
        line.trim();
        if (line.length() == 0) break;
        if (line.startsWith("Transfer-Encoding:") && line.indexOf("chunked") >= 0) {
            chunked = true;
        }
        if (line.startsWith("Content-Length:")) {
            contentLength = line.substring(15).toInt();
        }
    }

    String payload;

    if (chunked) {
        while (true) {
            String sizeLine;
            if (!read_header_line(client, sizeLine, deadline)) break;
            sizeLine.trim();
            long size = strtol(sizeLine.c_str(), NULL, 16);
            if (size <= 0) break;

            uint8_t buf[256];
            long left = size;
            while (left > 0) {
                if (client.available() > 0) {
                    int want = min((long)sizeof(buf), left);
                    int got = client.read(buf, want);
                    if (got > 0) {
                        payload += String((char*)buf, got);
                        left -= got;
                    }
                } else {
                    if (millis() > deadline) return payload;
                    delay(2);
                }
            }

            client.readStringUntil('\n');
        }
    } else if (contentLength >= 0) {
        while (payload.length() < (unsigned)contentLength) {
            if (client.available() > 0) {
                int c = client.read();
                if (c >= 0) payload += (char)c;
            } else {
                if (millis() > deadline ||
                    (!client.connected() && client.available() == 0)) {
                    break;
                }
                delay(2);
            }
        }
    } else {
        while (client.connected() || client.available() > 0) {
            if (client.available() > 0) {
                int c = client.read();
                if (c >= 0) payload += (char)c;
            } else {
                if (millis() > deadline) break;
                delay(2);
            }
        }
    }

    return payload;
}

LLMResult llm_chat(const String& userPrompt) {
    LLMResult r = { false, "", -1, "" };

    if (!wifi_is_connected()) {
        r.errorMsg = "WiFi not connected";
        return r;
    }

    if (currentProviderIdx < 0 || currentProviderIdx >= LLM_PROVIDER_COUNT) {
        r.errorMsg = "Invalid provider";
        return r;
    }

    LLMProvider& p = providers[currentProviderIdx];

    if (p.apiKey.length() == 0) {
        r.errorMsg = p.name + " API key empty (edit config.h)";
        return r;
    }

    // 构造请求 JSON
    JsonDocument req;
    req["model"] = p.model;
    req["messages"][0]["role"]    = "system";
    req["messages"][0]["content"] = LLM_SYSTEM_PROMPT;
    req["messages"][1]["role"]    = "user";
    req["messages"][1]["content"] = userPrompt;
    req["temperature"] = (float)LLM_TEMPERATURE;
    req["max_tokens"]  = LLM_MAX_TOKENS;

    String body;
    serializeJson(req, body);
    DBG_PRINTLN("[LLM] [" + p.name + "] POST " + p.endpoint);
    DBG_PRINTLN("[LLM] model=" + p.model + " body=" + String(body.length()) + "B");

    String host;
    int port = 443;
    String path;
    if (!parse_https_url(p.endpoint, host, port, path)) {
        r.errorMsg = "Invalid HTTPS endpoint";
        return r;
    }

    IPAddress ip;
    if (!WiFi.hostByName(host.c_str(), ip)) {
        r.errorMsg = "DNS fail: " + host;
        DBG_PRINTLN("[LLM] " + r.errorMsg);
        return r;
    }
    DBG_PRINTLN("[LLM] resolve " + host + " -> " + ip.toString());

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(LLM_TIMEOUT_MS / 1000);
    client.setHandshakeTimeout(15000);

    if (!client.connect(ip, port, host.c_str(), NULL, NULL, NULL)) {
        r.errorMsg = "TCP/TLS connect failed";
        DBG_PRINTLN("[LLM] " + r.errorMsg);
        return r;
    }
    client.setNoDelay(true);

    String request = "POST " + path + " HTTP/1.1\r\n";
    request += "Host: " + host;
    if (port != 443) request += ":" + String(port);
    request += "\r\nUser-Agent: ESP32-S3-AI\r\nConnection: close\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Authorization: Bearer " + p.apiKey + "\r\n";
    request += "Content-Length: " + String(body.length()) + "\r\n\r\n";
    request += body;
    client.print(request);

    int code = -1;
    String payload = read_http_response(client, code);
    client.stop();
    r.httpCode = code;

    if (code != 200) {
        r.errorMsg = (code <= 0) ? "HTTP read Timeout" : "HTTP " + String(code);
        DBG_PRINTLN("[LLM] error resp: " + payload.substring(0, 500));
        return r;
    }

    DBG_PRINTLN("[LLM] resp length: " + String(payload.length()));

    // 若响应过大 (无 PSRAM 时 ESP32 内存吃紧), 截断到 8KB 防止 JSON 解析失败
    if (payload.length() > 8192 && !psramFound()) {
        DBG_PRINTLN("[LLM] WARN: response truncated to 8KB (no PSRAM)");
        payload = payload.substring(0, 8192);
    }

    JsonDocument resp;
    DeserializationError err = deserializeJson(resp, payload);
    if (err) {
        r.errorMsg = "JSON parse: " + String(err.c_str());
        DBG_PRINTLN("[LLM] " + r.errorMsg);
        return r;
    }

    if (resp["error"]["message"].is<const char*>()) {
        r.errorMsg = "API: " + resp["error"]["message"].as<String>();
        DBG_PRINTLN("[LLM] " + r.errorMsg);
        return r;
    }

    r.content = resp["choices"][0]["message"]["content"].as<String>();
    r.ok = true;

    // trim 前后空白 (有些模型喜欢在回复开头加空行)
    r.content.trim();
    while (r.content.length() > 0 &&
           (r.content.charAt(0) == '\n' || r.content.charAt(0) == '\r' ||
            r.content.charAt(0) == ' '  || r.content.charAt(0) == '\t')) {
        r.content.remove(0, 1);
    }
    while (r.content.length() > 0 &&
           (r.content.charAt(r.content.length()-1) == '\n' ||
            r.content.charAt(r.content.length()-1) == '\r' ||
            r.content.charAt(r.content.length()-1) == ' '  ||
            r.content.charAt(r.content.length()-1) == '\t')) {
        r.content.remove(r.content.length()-1, 1);
    }

    DBG_PRINTLN("[LLM] ✅ reply length: " + String(r.content.length()));
    return r;
}
