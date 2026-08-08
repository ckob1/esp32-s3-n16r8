#include "music.h"
#include "audio.h"
#include "wifi_utils.h"
#include "cert_bundle.h"
#include "LittleFS.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "MP3DecoderHelix.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#define MUSIC_MAX_CLOUD    8
#define MUSIC_STALL_TIMEOUT_MS 12000
#define MUSIC_NET_TIMEOUT_MS   8000

static String g_names[MUSIC_MAX_FILES];
static String g_paths[MUSIC_MAX_FILES];
static int g_count = 0;

static String g_cloudNames[MUSIC_MAX_CLOUD];
static String g_cloudUrls[MUSIC_MAX_CLOUD];
static int g_cloudCount = 0;

static volatile bool g_playing = false;
static volatile bool g_stop = false;
static TaskHandle_t g_task = NULL;
static String g_currentName;
static SemaphoreHandle_t g_musicMutex = NULL;

static libhelix::MP3DecoderHelix g_cloudMp3;
static volatile bool g_cloudDecoding = false;
static uint32_t g_cloudFrames = 0;

static void music_scan() {
    g_count = 0;

    File root = LittleFS.open("/music");
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }

    File file = root.openNextFile();
    while (file && g_count < MUSIC_MAX_FILES) {
        if (!file.isDirectory()) {
            String name = file.name();
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            if (name.endsWith(".wav") || name.endsWith(".WAV")) {
                g_names[g_count] = name;
                String fullPath = file.name();
                if (!fullPath.startsWith("/")) {
                    fullPath = "/music/" + name;
                }
                g_paths[g_count] = fullPath;
                g_count++;
            }
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
}

static String name_from_url(const String& url) {
    String u = url;
    int q = u.indexOf('?');
    if (q >= 0) u = u.substring(0, q);
    int slash = u.lastIndexOf('/');
    if (slash >= 0) u = u.substring(slash + 1);
    u.replace("%20", " ");
    if (u.length() == 0) return "Cloud Track";
    return u;
}

static void music_scan_cloud() {
    g_cloudCount = 0;

    File f = LittleFS.open("/cloud.txt", "r");
    if (!f) return;

    while (f.available() && g_cloudCount < MUSIC_MAX_CLOUD) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#")) continue;

        int sep = line.indexOf('|');
        String name;
        String url;
        if (sep > 0) {
            name = line.substring(0, sep);
            name.trim();
            url = line.substring(sep + 1);
            url.trim();
        } else {
            url = line;
        }

        if (!url.startsWith("http://") && !url.startsWith("https://")) continue;
        if (name.length() == 0) name = name_from_url(url);
        if (name.length() > 40) name = name.substring(0, 40);

        g_cloudNames[g_cloudCount] = name;
        g_cloudUrls[g_cloudCount] = url;
        g_cloudCount++;
    }
    f.close();
}

static void cloud_pcm_callback(MP3FrameInfo& info,
                               int16_t* pcmBuffer, size_t len, void*) {
    if (!g_cloudDecoding || g_stop || !pcmBuffer || len == 0) return;
    g_cloudFrames += len;
    audio_play_pcm_interleaved(pcmBuffer, len, info.nChans, info.samprate);
}

static bool stream_mp3_url(const String& url) {
    if (!wifi_is_connected()) {
        Serial.println("[Music] cloud: WiFi not connected");
        return false;
    }

    WiFiClientSecure secure;
    WiFiClient plain;
    HTTPClient http;

    http.setTimeout(MUSIC_NET_TIMEOUT_MS);
    http.setConnectTimeout(10000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setRedirectLimit(4);
    http.setReuse(false);
    http.setUserAgent("ESP32-AI-OS/1.0");

    bool ok = false;
    if (url.startsWith("https://")) {
        secure.setCACertBundle(x509_crt_bundle);
        secure.setHandshakeTimeout(15000);
        ok = http.begin(secure, url);
    } else if (url.startsWith("http://")) {
        ok = http.begin(plain, url);
    } else {
        Serial.println("[Music] cloud: invalid URL");
        return false;
    }

    if (!ok) {
        Serial.println("[Music] cloud: HTTP begin failed");
        http.end();
        return false;
    }

    int code = http.GET();
    if (code != 200) {
        Serial.println("[Music] cloud: HTTP " + String(code));
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    if (!stream) {
        Serial.println("[Music] cloud: no stream");
        http.end();
        return false;
    }

    g_cloudFrames = 0;
    g_cloudDecoding = true;
    g_cloudMp3.setDataCallback(cloud_pcm_callback);
    if (!g_cloudMp3.begin()) {
        Serial.println("[Music] cloud: decoder init failed");
        g_cloudDecoding = false;
        http.end();
        return false;
    }

    uint8_t buf[1536];
    size_t total = 0;
    long size = http.getSize();
    uint32_t lastDataAt = millis();
    bool completed = false;

    while (!g_stop) {
        if (stream->available() > 0) {
            int avail = stream->available();
            if (avail > (int)sizeof(buf)) avail = sizeof(buf);
            int n = stream->read(buf, avail);
            if (n > 0) {
                g_cloudMp3.write(buf, n);
                g_cloudMp3.flush();
                total += n;
                lastDataAt = millis();
                continue;
            }
            if (n < 0) break;
        }

        if (size >= 0 && total >= (size_t)size) {
            completed = true;
            break;
        }
        if (!http.connected() && stream->available() == 0) {
            completed = true;
            break;
        }
        if (millis() - lastDataAt > MUSIC_STALL_TIMEOUT_MS) {
            Serial.println("[Music] cloud: stream stalled");
            break;
        }
        delay(2);
    }

    g_cloudMp3.flush();
    g_cloudMp3.end();
    audio_reset_output();
    g_cloudDecoding = false;
    http.end();

    if (g_stop) {
        Serial.println("[Music] cloud: stopped");
        return false;
    }
    if (!completed || total == 0 || g_cloudFrames == 0) {
        Serial.println("[Music] cloud: stream incomplete bytes=" +
                       String(total) + " frames=" + String(g_cloudFrames));
        return false;
    }
    return true;
}

static void music_task(void* pv) {
    int idx = (int)(intptr_t)pv;
    String path = g_paths[idx];

    g_stop = false;
    g_playing = true;

    bool ok = audio_play_wav_file(path.c_str(), &g_stop);
    if (!ok && !g_stop) Serial.println("[Music] playback failed");

    g_playing = false;
    g_task = NULL;
    vTaskDelete(NULL);
}

static void music_cloud_task(void* pv) {
    int idx = (int)(intptr_t)pv;
    int c = idx - g_count;

    g_stop = false;
    g_playing = true;
    if (g_musicMutex) xSemaphoreTake(g_musicMutex, portMAX_DELAY);
    g_currentName = g_cloudNames[c];
    if (g_musicMutex) xSemaphoreGive(g_musicMutex);

    bool ok = stream_mp3_url(g_cloudUrls[c]);
    if (!ok && !g_stop) Serial.println("[Music] cloud playback failed");

    g_playing = false;
    g_task = NULL;
    vTaskDelete(NULL);
}

void music_init() {
    if (!g_musicMutex) g_musicMutex = xSemaphoreCreateMutex();
    if (!LittleFS.begin(false)) {
        DBG_PRINTLN("[Music] formatting LittleFS...");
        LittleFS.begin(true);
    }
    music_scan();
    music_scan_cloud();
    DBG_PRINTLN("[Music] local=" + String(g_count) +
                " cloud=" + String(g_cloudCount));
}

int music_get_count() {
    return g_count + g_cloudCount;
}

String music_get_name(int idx) {
    if (idx < 0 || idx >= music_get_count()) return "";
    if (idx < g_count) return g_names[idx];
    return g_cloudNames[idx - g_count];
}

String music_get_path(int idx) {
    if (idx < 0 || idx >= music_get_count()) return "";
    if (idx < g_count) return g_paths[idx];
    return g_cloudUrls[idx - g_count];
}

bool music_is_cloud(int idx) {
    return idx >= g_count && idx < music_get_count();
}

int music_get_cloud_count() {
    return g_cloudCount;
}

String music_get_cloud_name(int idx) {
    if (idx < 0 || idx >= g_cloudCount) return "";
    return g_cloudNames[idx];
}

String music_get_cloud_url(int idx) {
    if (idx < 0 || idx >= g_cloudCount) return "";
    return g_cloudUrls[idx];
}

bool music_play(int idx) {
    if (idx < 0 || idx >= music_get_count()) return false;
    if (idx >= g_count && idx - g_count >= g_cloudCount) return false;
    if (idx >= g_count && !wifi_is_connected()) {
        Serial.println("[Music] cloud: no WiFi");
        return false;
    }

    if (g_playing) {
        music_stop();
        uint32_t t0 = millis();
        while (g_playing && millis() - t0 < 3000) {
            delay(10);
        }
        if (g_playing) {
            Serial.println("[Music] previous track still stopping");
            return false;
        }
    }

    if (g_musicMutex) xSemaphoreTake(g_musicMutex, portMAX_DELAY);
    g_currentName = music_get_name(idx);
    if (g_musicMutex) xSemaphoreGive(g_musicMutex);
    g_stop = false;

    int stack = (idx < g_count) ? 6144 : 16384;  // 云播任务内跑 TLS + MP3 解码, 需要大栈
    if (xTaskCreatePinnedToCore(idx < g_count ? music_task : music_cloud_task,
                                "music", stack, (void*)(intptr_t)idx, 1,
                                &g_task, 0) != pdPASS) {
        DBG_PRINTLN("[Music] task create failed");
        return false;
    }
    return true;
}

void music_stop() {
    // 只发停止请求, 不阻塞 UI; 播放任务会在下一个安全点退出
    g_stop = true;
}

bool music_is_playing() {
    return g_playing;
}

String music_get_current_name() {
    if (!g_musicMutex) return g_currentName;
    xSemaphoreTake(g_musicMutex, portMAX_DELAY);
    String n = g_currentName;
    xSemaphoreGive(g_musicMutex);
    return n;
}
