#include "tts.h"
#include "audio.h"
#include "music.h"
#include "wifi_utils.h"
#include "cert_bundle.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>
#include <time.h>
#include "MP3DecoderHelix.h"

static libhelix::MP3DecoderHelix g_mp3;
static bool g_ttsActive = false;
static volatile bool g_ttsStop = false;
static size_t g_ttsPcmFrames = 0;
static TaskHandle_t g_ttsTask = NULL;
static String g_ttsText;
static int16_t* g_resampleBuf = NULL;
static size_t g_resampleBufSamples = 0;

static bool tts_ensure_resample_buf(size_t frames) {
    size_t need = frames * 2 + 64;
    if (g_resampleBuf && g_resampleBufSamples >= need) return true;
    if (g_resampleBuf) heap_caps_free(g_resampleBuf);
    g_resampleBuf = (int16_t*)heap_caps_malloc(need * sizeof(int16_t),
                                               MALLOC_CAP_SPIRAM);
    if (!g_resampleBuf) {
        g_resampleBufSamples = 0;
        return false;
    }
    g_resampleBufSamples = need;
    return true;
}

static void tts_resample_24k_to_16k(const int16_t* in, size_t frames,
                                    int16_t* out, size_t& outFrames) {
    outFrames = (size_t)((uint64_t)frames * 16000 / 24000);
    for (size_t i = 0; i < outFrames; i++) {
        // 16.16 定点: srcPos = i * 24000 / 16000
        uint64_t srcPos = ((uint64_t)i * 24000) << 16;
        srcPos /= 16000;
        size_t idx = (size_t)(srcPos >> 16);
        uint32_t frac = (uint32_t)(srcPos & 0xFFFF);
        int16_t a = in[idx];
        int16_t b = (idx + 1 < frames) ? in[idx + 1] : a;
        out[i] = (int16_t)(a + (((int32_t)(b - a) * (int32_t)frac) >> 16));
    }
}

static void tts_data_callback(MP3FrameInfo& info,
                              int16_t* pcmBuffer, size_t len, void*) {
    if (!g_ttsActive || g_ttsStop || len == 0 || pcmBuffer == NULL) return;
    if (info.samprate == 24000 && info.nChans == 1) {
        if (!tts_ensure_resample_buf(len)) return;
        size_t outFrames = 0;
        tts_resample_24k_to_16k(pcmBuffer, len, g_resampleBuf, outFrames);
        g_ttsPcmFrames += outFrames;
        audio_play_pcm_interleaved(g_resampleBuf, outFrames, 1, 16000);
        return;
    }
    g_ttsPcmFrames += len;
    audio_play_pcm_interleaved(pcmBuffer, len, info.nChans, info.samprate);
}

static String tts_url_encode(const String& s) {
    const char* hex = "0123456789ABCDEF";
    String out;
    for (unsigned i = 0; i < s.length(); i++) {
        unsigned char c = (unsigned char)s[i];
        bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                    c == '.' || c == '~';
        if (safe) {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

static String tts_json_escape(const String& s) {
    String out;
    for (unsigned i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char tmp[8];
                    snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned char)c);
                    out += tmp;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

static bool tts_has_chinese(const String& s) {
    // UTF-8 Chinese characters are multi-byte with a lead byte >= 0xE4.
    for (unsigned i = 0; i + 2 < s.length(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0xE4 && c <= 0xE9) return true;
    }
    return false;
}

static String tts_xml_escape(const String& s) {
    String out;
    for (unsigned i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;
        }
    }
    return out;
}

static String edge_base64_encode(const uint8_t* data, size_t len) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String out;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16;
        if (i + 1 < len) v |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) v |= data[i + 2];
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += (i + 1 < len) ? tbl[(v >> 6) & 63] : '=';
        out += (i + 2 < len) ? tbl[v & 63] : '=';
    }
    return out;
}

static String edge_sha256_hex(const String& in) {
    unsigned char hash[32];
    mbedtls_sha256((const unsigned char*)in.c_str(), in.length(), hash, 0);
    String out;
    const char* hex = "0123456789ABCDEF";
    for (int i = 0; i < 32; i++) {
        out += hex[hash[i] >> 4];
        out += hex[hash[i] & 0x0F];
    }
    return out;
}

static String edge_sec_ms_gec(time_t now) {
    int64_t ticks = (int64_t)now + 11644473600LL;
    ticks -= ticks % 300;
    ticks *= 10000000LL;
    char num[24];
    snprintf(num, sizeof(num), "%lld", (long long)ticks);
    return edge_sha256_hex(String(num) + "6A5AA1D4EAFF4E9FB37E23D68491D6F4");
}

static String edge_http_date(time_t now) {
    struct tm* tm = gmtime(&now);
    char buf[64];
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", tm);
    return String(buf) + "+0000 (Coordinated Universal Time)";
}

static String edge_random_hex(size_t bytes) {
    String out;
    const char* hex = "0123456789ABCDEF";
    for (size_t i = 0; i < bytes; i++) {
        uint8_t b = (uint8_t)esp_random();
        out += hex[b >> 4];
        out += hex[b & 0x0F];
    }
    return out;
}

static bool edge_read_header_line(WiFiClientSecure& client, String& out, uint32_t deadline) {
    out = "";
    while (out.length() < 8192) {
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

static bool edge_ws_read_exact(WiFiClientSecure& client, uint8_t* buf, size_t len,
                               uint32_t deadline) {
    size_t got = 0;
    while (got < len) {
        if (client.available() > 0) {
            int n = client.read(buf + got, len - got);
            if (n > 0) got += n;
            else if (n < 0) return false;
        } else {
            if (!client.connected() && client.available() == 0) return false;
            if (millis() > deadline) return false;
            delay(2);
        }
    }
    return true;
}

static bool edge_ws_send_frame(WiFiClientSecure& client, uint8_t opcode,
                               const uint8_t* data, size_t len) {
    uint8_t header[14];
    size_t hlen = 2;
    header[0] = 0x80 | opcode;
    if (len <= 125) {
        header[1] = 0x80 | (uint8_t)len;
    } else if (len <= 65535) {
        header[1] = 0x80 | 126;
        header[2] = (uint8_t)(len >> 8);
        header[3] = (uint8_t)len;
        hlen = 4;
    } else {
        header[1] = 0x80 | 127;
        uint64_t v = len;
        for (int i = 0; i < 8; i++) {
            header[2 + i] = (uint8_t)(v >> (56 - i * 8));
        }
        hlen = 10;
    }

    uint8_t mask[4];
    for (int i = 0; i < 4; i++) mask[i] = (uint8_t)esp_random();
    memcpy(header + hlen, mask, 4);
    hlen += 4;

    if (client.write(header, hlen) != hlen) return false;

    uint8_t chunk[256];
    size_t off = 0;
    while (off < len) {
        size_t n = min((size_t)sizeof(chunk), len - off);
        for (size_t i = 0; i < n; i++) {
            chunk[i] = data[off + i] ^ mask[(off + i) & 3];
        }
        if (client.write(chunk, n) != n) return false;
        off += n;
    }
    return true;
}

static bool edge_ws_send_text(WiFiClientSecure& client, const String& text) {
    return edge_ws_send_frame(client, 0x01,
                              (const uint8_t*)text.c_str(), text.length());
}

static bool edge_ws_read_frame(WiFiClientSecure& client, uint8_t*& payload,
                               size_t& payloadLen, uint8_t& opcode,
                               uint32_t deadline) {
    payload = NULL;
    payloadLen = 0;
    opcode = 0;

    uint8_t hdr[2];
    if (!edge_ws_read_exact(client, hdr, 2, deadline)) return false;
    opcode = hdr[0] & 0x0F;
    bool masked = (hdr[1] & 0x80) != 0;
    size_t len = hdr[1] & 0x7F;

    if (len == 126) {
        uint8_t ext[2];
        if (!edge_ws_read_exact(client, ext, 2, deadline)) return false;
        len = ((size_t)ext[0] << 8) | ext[1];
    } else if (len == 127) {
        uint8_t ext[8];
        if (!edge_ws_read_exact(client, ext, 8, deadline)) return false;
        len = 0;
        for (int i = 0; i < 8; i++) len = (len << 8) | ext[i];
    }

    if (len > 512 * 1024) return false;

    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked && !edge_ws_read_exact(client, mask, 4, deadline)) return false;

    uint8_t* buf = (uint8_t*)heap_caps_malloc(len ? len : 1, MALLOC_CAP_SPIRAM);
    if (!buf) return false;
    if (len > 0 && !edge_ws_read_exact(client, buf, len, deadline)) {
        heap_caps_free(buf);
        return false;
    }
    if (masked) {
        for (size_t i = 0; i < len; i++) buf[i] ^= mask[i & 3];
    }

    payload = buf;
    payloadLen = len;
    return true;
}

static bool edge_tts_fetch_direct(const String& text, uint8_t*& data,
                                  size_t& dataLen, bool liveDecode) {
    data = NULL;
    dataLen = 0;

    if (!wifi_is_connected()) {
        Serial.println("[TTS] Edge: WiFi not connected");
        return false;
    }

    time_t now = time(NULL);
    if (now < 1600000000) {
        for (int i = 0; i < 20 && now < 1600000000; i++) {
            delay(250);
            now = time(NULL);
        }
        if (now < 1600000000) {
            Serial.println("[TTS] Edge: NTP time not ready");
            return false;
        }
    }

    WiFiClientSecure client;
    client.setCACertBundle(x509_crt_bundle);
    client.setTimeout(10);
    client.setHandshakeTimeout(15000);
    if (!client.connect("speech.platform.bing.com", 443)) {
        Serial.println("[TTS] Edge: TLS connect failed");
        return false;
    }
    client.setNoDelay(true);

    String conn = edge_random_hex(16);
    String token = edge_sec_ms_gec(now);
    String path = "/consumer/speech/synthesize/readaloud/edge/v1?";
    path += "TrustedClientToken=6A5AA1D4EAFF4E9FB37E23D68491D6F4";
    path += "&Sec-MS-GEC=" + token;
    path += "&Sec-MS-GEC-Version=1-143.0.3650.75";
    path += "&ConnectionId=" + conn;

    uint8_t wsKey[16];
    for (int i = 0; i < 16; i++) wsKey[i] = (uint8_t)esp_random();
    String keyB64 = edge_base64_encode(wsKey, sizeof(wsKey));

    String req = "GET " + path + " HTTP/1.1\r\n";
    req += "Host: speech.platform.bing.com\r\n";
    req += "Upgrade: websocket\r\nConnection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + keyB64 + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n";
    req += "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36 Edg/143.0.0.0\r\n";
    req += "Origin: chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold\r\n";
    req += "Pragma: no-cache\r\nCache-Control: no-cache\r\n";
    req += "Cookie: muid=" + edge_random_hex(16) + "\r\n\r\n";
    client.print(req);

    uint32_t deadline = millis() + TTS_TIMEOUT_MS;
    String status;
    if (!edge_read_header_line(client, status, deadline) ||
        status.indexOf(" 101 ") < 0) {
        Serial.println("[TTS] Edge: handshake failed: " + status);
        client.stop();
        return false;
    }
    while (true) {
        String line;
        if (!edge_read_header_line(client, line, deadline)) {
            client.stop();
            return false;
        }
        line.trim();
        if (line.length() == 0) break;
    }

    String ts = edge_http_date(now);
    String config = "X-Timestamp:" + ts + "\r\n";
    config += "Content-Type:application/json; charset=utf-8\r\n";
    config += "Path:speech.config\r\n\r\n";
    config += "{\"context\":{\"synthesis\":{\"audio\":{\"metadataoptions\":{\"sentenceBoundaryEnabled\":\"false\",\"wordBoundaryEnabled\":\"true\"},\"outputFormat\":\"audio-24khz-96kbitrate-mono-mp3\"}}}}\r\n";
    if (!edge_ws_send_text(client, config)) {
        client.stop();
        return false;
    }

    String lang = String(TTS_VOICE);
    int dash1 = lang.indexOf('-');
    int dash2 = dash1 >= 0 ? lang.indexOf('-', dash1 + 1) : -1;
    if (dash2 > 0) lang = lang.substring(0, dash2);
    if (lang.length() == 0) lang = "zh-CN";

    String ssml = "<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='" + lang + "'>";
    ssml += "<voice name='" + String(TTS_VOICE) + "'><prosody pitch='+0Hz' rate='+0%' volume='+0%'>";
    ssml += tts_xml_escape(text);
    ssml += "</prosody></voice></speak>";

    String msg = "X-RequestId:" + conn + "\r\n";
    msg += "Content-Type:application/ssml+xml\r\n";
    msg += "X-Timestamp:" + ts + "Z\r\n";
    msg += "Path:ssml\r\n\r\n" + ssml;
    if (!edge_ws_send_text(client, msg)) {
        client.stop();
        return false;
    }

    if (!liveDecode) {
        data = (uint8_t*)heap_caps_malloc(TTS_MAX_AUDIO_BYTES, MALLOC_CAP_SPIRAM);
        if (!data) {
            client.stop();
            return false;
        }
    }

    size_t got = 0;
    bool turnEnd = false;
    while (millis() < deadline && !turnEnd && !g_ttsStop) {
        uint8_t* frame = NULL;
        size_t frameLen = 0;
        uint8_t opcode = 0;
        if (!edge_ws_read_frame(client, frame, frameLen, opcode, deadline)) break;

        if (opcode == 0x01) {
            String payload((const char*)frame, frameLen);
            int sep = payload.indexOf("\r\n\r\n");
            String headers = sep >= 0 ? payload.substring(0, sep) : payload;
            if (headers.indexOf("Path:turn.end") >= 0) turnEnd = true;
        } else if (opcode == 0x02) {
            if (frameLen >= 2) {
                size_t headerLen = ((size_t)frame[0] << 8) | frame[1];
                size_t audioStart = 2 + headerLen;
                if (audioStart < frameLen) {
                    size_t n = frameLen - audioStart;
                    if (liveDecode) {
                        g_mp3.write(frame + audioStart, n);
                        g_mp3.flush();
                        got += n;
                    } else {
                        if (got + n > TTS_MAX_AUDIO_BYTES) {
                            heap_caps_free(frame);
                            break;
                        }
                        memcpy(data + got, frame + audioStart, n);
                        got += n;
                    }
                }
            }
        } else if (opcode == 0x09) {
            edge_ws_send_frame(client, 0x0A, frame, frameLen);
        } else if (opcode == 0x08) {
            turnEnd = true;
        }

        heap_caps_free(frame);
    }
    client.stop();

    if (liveDecode) {
        client.stop();
        Serial.println("[TTS] Edge stream bytes=" + String(got) +
                       " pcm=" + String(g_ttsPcmFrames));
        return g_ttsPcmFrames > 0;
    }

    if (got == 0) {
        Serial.println("[TTS] Edge: no audio received");
        heap_caps_free(data);
        data = NULL;
        return false;
    }

    dataLen = got;
    Serial.println("[TTS] Edge direct bytes=" + String(got));
    return true;
}

static bool tts_fetch(const String& text, uint8_t*& data, size_t& dataLen) {
    data = NULL;
    dataLen = 0;

    if (!wifi_is_connected()) {
        Serial.println("[TTS] WiFi not connected");
        return false;
    }

    if (TTS_MODE == 2) {
        return edge_tts_fetch_direct(text, data, dataLen, false);
    }

    String url = String(TTS_ENDPOINT);
#ifdef TTS_ENDPOINT_ZH
    if (tts_has_chinese(text)) {
        url = String(TTS_ENDPOINT_ZH);
    }
#endif
    if (TTS_MODE == 0) {
        url += tts_url_encode(text);
    }

    WiFiClientSecure client;
    client.setCACertBundle(x509_crt_bundle);
    client.setHandshakeTimeout(15000);

    HTTPClient http;
    http.setTimeout(TTS_TIMEOUT_MS);
    http.setConnectTimeout(10000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setRedirectLimit(3);
    http.setReuse(false);
    http.setUserAgent("ESP32-AI-OS/1.0");

    if (!http.begin(client, url)) {
        Serial.println("[TTS] HTTP begin failed");
        http.end();
        return false;
    }

    int code;
    if (TTS_MODE == 0) {
        code = http.GET();
    } else {
        String body = "{\"model\":\"" + String(TTS_MODEL) +
                      "\",\"input\":\"" + tts_json_escape(text) +
                      "\",\"voice\":\"" + String(TTS_VOICE) +
                      "\",\"response_format\":\"mp3\"}";
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Accept", "audio/mpeg");
        if (strlen(TTS_API_KEY) > 0) {
            http.addHeader("Authorization", "Bearer " + String(TTS_API_KEY));
        }
        code = http.POST(body);
    }

    if (code != 200) {
        Serial.println("[TTS] HTTP " + String(code));
        http.end();
        return false;
    }

    long size = http.getSize();
    if (size > (long)TTS_MAX_AUDIO_BYTES) {
        Serial.println("[TTS] audio too large: " + String(size));
        http.end();
        return false;
    }

    size_t maxBytes = (size > 0) ? (size_t)size : TTS_MAX_AUDIO_BYTES;
    data = (uint8_t*)heap_caps_malloc(maxBytes, MALLOC_CAP_SPIRAM);
    if (!data) {
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    if (!stream) {
        Serial.println("[TTS] no response stream");
        heap_caps_free(data);
        data = NULL;
        http.end();
        return false;
    }

    size_t got = 0;
    uint32_t deadline = millis() + TTS_TIMEOUT_MS;
    while (got < maxBytes) {
        if (stream->available() > 0) {
            int n = stream->read(data + got, maxBytes - got);
            if (n > 0) {
                got += n;
            } else if (n < 0) {
                break;
            }
        } else {
            if (!http.connected() && stream->available() == 0) break;
            if (millis() > deadline) break;
            delay(2);
        }
    }
    http.end();

    if (size > 0 && got != (size_t)size) {
        Serial.println("[TTS] read incomplete: " + String(got) + "/" + String(size));
        heap_caps_free(data);
        data = NULL;
        return false;
    }
    if (got == 0) {
        Serial.println("[TTS] empty response");
        heap_caps_free(data);
        data = NULL;
        return false;
    }

    dataLen = got;
    return true;
}

static bool tts_speak_sync(const String& text) {
    if (g_ttsActive) return false;

    String t = text;
    t.trim();
    if (t.length() == 0) return false;
    if (t.length() > TTS_MAX_TEXT) t = t.substring(0, TTS_MAX_TEXT);

    if (music_is_playing()) {
        music_stop();
        uint32_t t0 = millis();
        while (music_is_playing() && millis() - t0 < 3000) delay(5);
        if (music_is_playing()) {
            Serial.println("[TTS] music still stopping");
            return false;
        }
    }

    if (TTS_MODE == 2) {
        g_ttsActive = true;
        g_ttsStop = false;
        g_ttsPcmFrames = 0;
        audio_reset_output();
        g_mp3.setDataCallback(tts_data_callback);
        if (!g_mp3.begin()) {
            g_ttsActive = false;
            Serial.println("[TTS] decoder init failed");
            return false;
        }

        uint8_t* unused = NULL;
        size_t unusedLen = 0;
        bool ok = edge_tts_fetch_direct(t, unused, unusedLen, true);
        g_mp3.flush();
        g_mp3.end();
        audio_reset_output();
        g_ttsActive = false;
        g_ttsStop = false;
        if (g_resampleBuf) {
            heap_caps_free(g_resampleBuf);
            g_resampleBuf = NULL;
            g_resampleBufSamples = 0;
        }

        if (!ok) Serial.println("[TTS] decode/playback failed");
        return ok;
    }

    uint8_t* mp3 = NULL;
    size_t mp3Len = 0;
    if (!tts_fetch(t, mp3, mp3Len)) return false;

    g_ttsActive = true;
    g_ttsStop = false;
    g_ttsPcmFrames = 0;
    bool ok = false;
    g_mp3.setDataCallback(tts_data_callback);
    if (g_mp3.begin()) {
        g_mp3.write(mp3, mp3Len);
        g_mp3.flush();
        ok = g_ttsPcmFrames > 0;
        g_mp3.end();
    } else {
        Serial.println("[TTS] decoder init failed");
    }

    heap_caps_free(mp3);
    audio_reset_output();
    g_ttsActive = false;
    g_ttsStop = false;
    if (g_resampleBuf) {
        heap_caps_free(g_resampleBuf);
        g_resampleBuf = NULL;
        g_resampleBufSamples = 0;
    }

    if (!ok) Serial.println("[TTS] decode/playback failed");
    return ok;
}

bool tts_is_busy() {
    return g_ttsTask != NULL;
}

void tts_stop() {
    g_ttsStop = true;
}

static void tts_task(void*) {
    tts_speak_sync(g_ttsText);
    g_ttsTask = NULL;
    vTaskDelete(NULL);
}

bool tts_speak(const String& text) {
    if (tts_is_busy()) return false;
    g_ttsText = text;
    g_ttsStop = false;
    g_ttsActive = false;
    if (xTaskCreatePinnedToCore(tts_task, "tts", 16384, NULL, 1,
                                &g_ttsTask, 1) != pdPASS) {
        g_ttsTask = NULL;
        return false;
    }
    return true;
}
