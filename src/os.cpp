#include "os.h"
#include "config.h"
#include "display.h"
#include "ascii_font.h"
#include "touch.h"
#include "touch_calib.h"
#include "audio.h"
#include "wifi_utils.h"
#include "llm.h"
#include "web_server.h"
#include "weather.h"
#include "music.h"
#include "tetris.h"
#include "tts.h"
#include "voice.h"
#include "logger.h"
#include "ble_provision.h"
#include "ota.h"
#include "apple_logo.h"
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <driver/adc.h>
#include <math.h>
#include <time.h>

enum AppScreen {
    SCREEN_HOME,
    SCREEN_APPS,
    SCREEN_CHAT,
    SCREEN_TERMINAL,
    SCREEN_SETTINGS,
    SCREEN_WIFI,
    SCREEN_WIFI_CONNECT,
    SCREEN_WIFI_HISTORY,
    SCREEN_BLE,
    SCREEN_LOGS,
    SCREEN_INFO,
    SCREEN_WEATHER,
    SCREEN_WIFI_ANALYZER,
    SCREEN_MUSIC,
    SCREEN_TETRIS,
    SCREEN_CONTINUITY
};

static AppScreen g_screen = SCREEN_HOME;
static bool g_llmBusy = false;

static String g_chatLog;
static String g_chatInput;
static String g_lastAiReply;
static bool g_keyboardVisible = true;
static int g_chatScroll = 0;
static String g_termLog;
static String g_termInput;
static String g_wifiSelectedSsid;
static String g_wifiPassInput;
static String g_wifiNetworks[16];
static int g_wifiNetworkCount = 0;
static int g_wifiScroll = 0;
struct WifiNet {
    String ssid;
    String auth;
    int rssi;
    int channel;
};
static WifiNet g_wifiDetails[16];
static int g_wifiMapScroll = 0;
static BleScanEntry g_bleDevices[BLE_SCAN_MAX];
static int g_bleCount = 0;

static Button g_back;
static Button g_homeButtons[6];
static Button g_settingsButtons[8];
static Button g_wifiButtons[4];
static Button g_wifiNetworkButtons[5];
static Button g_wifiNav[2];
static Button g_wifiMapNav[2];
static Button g_chatActions[6];
static Button g_wifiConnectActions[2];
static Button g_bleButtons[BLE_SCAN_MAX];
static Button g_bleRescan;
static Button g_weatherButtons[1];
static Button g_continuityButtons[1];
static Button g_monitorNav[2];
static Button g_musicButtons[6];
static Button g_musicActions[6];
static Button g_appsButtons[7];
static int g_musicScroll = 0;

static Button g_keys[40];
static int g_keyCount = 0;

static String serialBuf;
static uint32_t g_lastWeatherCheck = 0;
static uint32_t g_wifiScanAt = 0;
static uint32_t g_bleScanAt = 0;
static String g_lastLogText;
static int g_weatherAnim = 0;
static int g_weatherScroll = 0;
static int g_monitorScroll = 0;
static bool g_contLastOk = false;
static bool g_contBeep = true;
static bool g_voiceActive = false;

static String mask_pass(const String& pass) {
    // 安全: UI/串口只显示掩码, 明文密码仅用于内部连接与 NVS 存取
    return pass.length() > 0 ? "********" : "(none)";
}

static void draw_home_screen();
static void draw_apps_screen();
static void draw_chat_screen();
static void draw_terminal_screen();
static void draw_settings_screen();
static void draw_wifi_screen();
static void refresh_ble_devices();
static void draw_ble_screen();
static void draw_logs_screen();
static void update_logs_view();
static void draw_info_screen();
static void draw_weather_screen();
static void draw_wifi_analyzer_screen();
static void draw_music_screen();
static void draw_tetris_screen();
static void draw_continuity_screen();

static void trim_log(String& s, int maxLen) {
    if ((int)s.length() > maxLen) {
        s = s.substring(s.length() - maxLen);
    }
}

static bool hit_button(const Button& b, int x, int y) {
    return display_point_in_button(b, x, y);
}

static void draw_bounded_ascii(int x, int y, int maxW, int maxH, const String& text, uint16_t color) {
    const int lineH = 10;
    int cx = x;
    int cy = y;
    int line = 0;
    int maxLines = maxH / lineH;

    for (int i = 0; i < (int)text.length() && line < maxLines; i++) {
        char c = text[i];
        if (c == '\n') {
            cx = x;
            cy += lineH;
            line++;
            continue;
        }
        if (c < 0x20 || c > 0x7E) continue;

        if (cx + 6 > maxW) {
            cx = x;
            cy += lineH;
            line++;
            if (line >= maxLines) break;
        }
        tft.drawChar(cx, cy, c, color, ILI9341_BLACK, 1);
        cx += 6;
    }
}

static void draw_boot_static() {
    uint16_t* buf = (uint16_t*)heap_caps_malloc(
        APPLE_LOGO_W * APPLE_LOGO_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!buf) return;

    for (int i = 0; i < APPLE_LOGO_W * APPLE_LOGO_H; i++) {
        buf[i] = pgm_read_word(&APPLE_LOGO_DATA[i]);
    }

    tft.fillScreen(ILI9341_BLACK);

    int logoX = (tft.width() - APPLE_LOGO_W) / 2;
    int logoY = (tft.height() - APPLE_LOGO_H) / 2;
    tft.drawRGBBitmap(logoX, logoY, buf, APPLE_LOGO_W, APPLE_LOGO_H);
    display_draw_text(112, 190, "ESP32 AI OS", ILI9341_WHITE, 1);
    tft.drawRect(100, 214, 120, 2, ILI9341_WHITE);

    heap_caps_free(buf);
}

static void draw_boot_progress(int frame, int frames) {
    const int barX = 100;
    const int barY = 214;
    const int barW = 120;
    int fillW = (frame + 1) * barW / frames;
    tft.fillRect(barX, barY, barW, 2, ILI9341_BLACK);
    tft.fillRect(barX, barY, fillW, 2, ILI9341_WHITE);
}

static void show_boot_animation() {
    const int frames = 30;
    draw_boot_static();
    for (int i = 0; i < frames; i++) {
        draw_boot_progress(i, frames);
        delay(60);
    }
}

static void draw_header(const String& title) {
    tft.fillScreen(ILI9341_BLACK);
    tft.fillRect(0, 0, tft.width(), 24, ILI9341_BLACK);
    tft.drawFastHLine(0, 23, tft.width(), ILI9341_GREEN);

    g_back = { 4, 4, 48, 18, "Back" };
    tft.fillRect(g_back.x, g_back.y, g_back.w, g_back.h, ILI9341_BLACK);
    tft.drawRect(g_back.x, g_back.y, g_back.w, g_back.h, ILI9341_GREEN);
    display_draw_text(9, 7, "Back", ILI9341_GREEN, 1);

    display_draw_text(58, 5, title, ILI9341_GREEN, 2);
    tft.drawFastHLine(0, tft.height() - 18, tft.width(), ILI9341_GREEN);
}

static Button draw_key(int x, int y, int w, int h, const String& label) {
    tft.fillRect(x, y, w, h, ILI9341_BLACK);
    tft.drawRect(x, y, w, h, ILI9341_GREEN);

    int tw = cn_string_width(label, 1);
    int cx = x + (w - tw) / 2;
    int cy = y + (h - 8) / 2;
    if (cx < x + 1) cx = x + 1;
    if (cy < y + 1) cy = y + 1;
    cn_draw_string(cx, cy, label, ILI9341_GREEN, ILI9341_BLACK, 1, x + w - 2);
    return { x, y, w, h, label };
}

static Button draw_app_button(int x, int y, int w, int h, const String& label) {
    tft.fillRect(x, y, w, h, ILI9341_BLACK);
    tft.drawRect(x, y, w, h, ILI9341_GREEN);

    int tw = cn_string_width(label, 1);
    int cx = x + (w - tw) / 2;
    int cy = y + (h - 8) / 2;
    if (cx < x + 1) cx = x + 1;
    if (cy < y + 1) cy = y + 1;
    cn_draw_string(cx, cy, label, ILI9341_GREEN, ILI9341_BLACK, 1, x + w - 2);
    return { x, y, w, h, label };
}

static void add_key(int x, int y, int w, int h, const String& label) {
    if (g_keyCount >= 40) return;
    g_keys[g_keyCount++] = draw_key(x, y, w, h, label);
}

static void draw_keyboard() {
    const int keyW = 29;
    const int keyH = 16;
    const int gap = 1;
    const int x0 = 2;
    const int y0 = 146;

    tft.fillRect(0, y0 - 2, tft.width(), tft.height() - (y0 - 2), ILI9341_BLACK);
    g_keyCount = 0;

    const char* row1 = "1234567890";
    const char* row2 = "qwertyuiop";
    const char* row3 = "asdfghjkl";
    const char* row4 = "zxcvbnm";
    const char* row5 = ".-/_:@?=+*";

    for (int i = 0; i < 10; i++) {
        add_key(x0 + i * (keyW + gap), y0, keyW, keyH, String(row1[i]));
    }
    for (int i = 0; i < 10; i++) {
        add_key(x0 + i * (keyW + gap), y0 + (keyH + gap), keyW, keyH, String(row2[i]));
    }
    for (int i = 0; i < 9; i++) {
        add_key(x0 + i * (keyW + gap), y0 + 2 * (keyH + gap), keyW, keyH, String(row3[i]));
    }
    add_key(x0 + 9 * (keyW + gap), y0 + 2 * (keyH + gap), keyW, keyH, "<-");

    for (int i = 0; i < 7; i++) {
        add_key(x0 + i * (keyW + gap), y0 + 3 * (keyH + gap), keyW, keyH, String(row4[i]));
    }
    add_key(221, y0 + 3 * (keyH + gap), 46, keyH, "Space");
    add_key(269, y0 + 3 * (keyH + gap), 49, keyH, "Send");

    for (int i = 0; i < 10; i++) {
        add_key(x0 + i * (keyW + gap), y0 + 4 * (keyH + gap), keyW, keyH, String(row5[i]));
    }
}

static String provider_list_text() {
    String s;
    for (int i = 0; i < LLM_PROVIDER_COUNT; i++) {
        bool ready = providers[i].apiKey.length() > 0;
        String mark = (i == currentProviderIdx) ? "*" : " ";
        s += mark + " [" + String(i) + "] " + providers[i].name +
             " model=" + providers[i].model +
             (ready ? " OK" : " NO KEY") + "\n";
    }
    return s;
}

static String history_pass_for_ssid(const String& ssid) {
    int n = wifi_history_count();
    for (int i = 0; i < n; i++) {
        if (wifi_history_ssid(i) == ssid) {
            return wifi_history_pass(i);
        }
    }
    return "";
}

static String format_hhmmss() {
    time_t now;
    if (time(&now) < 1600000000) return "NO TIME";
    struct tm* t = localtime(&now);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
    return String(buf);
}

static void update_home_clock() {
    if (g_screen != SCREEN_HOME) return;
    tft.fillRect(240, 4, 76, 12, ILI9341_BLACK);
    display_draw_text(246, 6, format_hhmmss(), ILI9341_GREEN, 1);
}

static float read_supply_voltage() {
    if (VOLT_SENSE_PIN < 0) return -1;
    analogSetPinAttenuation(VOLT_SENSE_PIN, ADC_11db);
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) {
        sum += analogReadMilliVolts(VOLT_SENSE_PIN);
        delay(2);
    }
    return (sum / 8) * VOLT_SENSE_SCALE / 1000.0f;
}

static void draw_metric_bar(int x, int y, int w, int h, uint32_t used, uint32_t total) {
    tft.drawRect(x, y, w, h, ILI9341_GREEN);
    if (total == 0) return;
    int fillW = (uint32_t)used * w / total;
    if (fillW > w) fillW = w;
    tft.fillRect(x, y, fillW, h, ILI9341_GREEN);
}

static int count_newlines(const String& s) {
    int n = 0;
    for (int i = 0; i < (int)s.length(); i++) {
        if (s[i] == '\n') n++;
    }
    return n;
}

static String skip_newlines(const String& s, int skip) {
    int pos = 0;
    int seen = 0;
    while (pos < (int)s.length() && seen < skip) {
        if (s[pos] == '\n') seen++;
        pos++;
    }
    return s.substring(pos);
}

static void draw_chat_body(int maxH) {
    int total = count_newlines(g_chatLog);
    if (g_chatScroll > total) g_chatScroll = total;
    if (g_chatScroll < 0) g_chatScroll = 0;

    tft.fillRect(0, 26, tft.width(), maxH, ILI9341_BLACK);
    draw_bounded_ascii(8, 30, 312, maxH - 4, skip_newlines(g_chatLog, g_chatScroll), ILI9341_GREEN);
}

static void draw_chat_action_bar(int y) {
    const int w = 47;
    const int h = 20;
    const int gap = 3;
    const int x0 = 8;

    tft.fillRect(0, y - 2, tft.width(), h + 4, ILI9341_BLACK);
    g_chatActions[0] = draw_app_button(x0, y, w, h, "New");
    g_chatActions[1] = draw_app_button(x0 + (w + gap), y, w, h, "Up");
    g_chatActions[2] = draw_app_button(x0 + 2 * (w + gap), y, w, h, "Down");
    g_chatActions[3] = draw_app_button(x0 + 3 * (w + gap), y, w, h, "Input");
    g_chatActions[4] = draw_app_button(x0 + 4 * (w + gap), y, w, h, "Speak");
    g_chatActions[5] = draw_app_button(
        x0 + 5 * (w + gap), y, w, h,
        (g_voiceActive || tts_is_busy()) ? "Stop" : "Voice");
}

static String extract_ai_display(const String& reply) {
    int en = reply.indexOf("EN:");
    if (en < 0) en = reply.indexOf("EN：");
    if (en >= 0) {
        int zh = reply.indexOf("ZH:");
        if (zh < 0) zh = reply.indexOf("ZH：");
        if (zh > en) {
            String s = reply.substring(en + 3, zh);
            s.trim();
            if (s.length() > 0) return s;
        }
    }
    return "Chinese reply (press Speak)";
}

static String extract_ai_tts(const String& reply) {
    int zh = reply.indexOf("ZH:");
    if (zh < 0) zh = reply.indexOf("ZH：");
    if (zh >= 0) {
        String s = reply.substring(zh + 3);
        s.trim();
        if (s.length() > 0) return s;
    }
    return reply;
}

static void draw_home_screen() {
    tft.fillScreen(ILI9341_BLACK);
    tft.fillRect(0, 0, tft.width(), 24, ILI9341_BLACK);
    tft.drawFastHLine(0, 23, tft.width(), ILI9341_GREEN);
    display_draw_text(8, 5, "ESP32 AI OS", ILI9341_GREEN, 2);
    display_draw_text(246, 6, format_hhmmss(), ILI9341_GREEN, 1);
    tft.drawFastHLine(0, tft.height() - 18, tft.width(), ILI9341_GREEN);

    String ip = wifi_is_ap_mode() ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    String info = "WiFi: " + wifi_get_ssid_display() + " (" + ip + ")";
    display_draw_text(8, 28, info, ILI9341_GREEN, 1);

    const int w = 96;
    const int h = 38;
    const int gap = 8;
    const int x0 = 8;
    const int y1 = 44;
    const int y2 = y1 + h + gap;

    g_homeButtons[0] = draw_app_button(x0, y1, w, h, "Chat");
    g_homeButtons[1] = draw_app_button(x0 + (w + gap), y1, w, h, "Terminal");
    g_homeButtons[2] = draw_app_button(x0 + 2 * (w + gap), y1, w, h, "Settings");
    g_homeButtons[3] = draw_app_button(x0, y2, w, h, "WiFi");
    g_homeButtons[4] = draw_app_button(x0 + (w + gap), y2, w, h, "Apps");
    g_homeButtons[5] = draw_app_button(x0 + 2 * (w + gap), y2, w, h, "Monitor");

    if (wifi_is_ap_mode()) {
        // 配网服务只在 AP 模式开放, 避免 STA 模式下局域网内任意访问
        display_draw_text(8, 140, "HTTP: http://" + ip, ILI9341_CYAN, 1);
        display_draw_text(8, 154, "BLE: ESP32-AI-Setup", ILI9341_CYAN, 1);
    }
    display_draw_text(8, 168, "Type any text in serial to chat", ILI9341_GREEN, 1);
    display_draw_text(8, 224, "Ready", ILI9341_GREEN, 1);
    display_draw_text(180, 224, "Provider: " + llm_get_provider_name(), ILI9341_GREEN, 1);
}

static void draw_apps_screen() {
    draw_header("Apps");

    const char* labels[7] = {
        "Weather", "WiFi Map", "Music", "Tetris",
        "Logs", "BLE", "Info"
    };
    const int w = 304;
    const int h = 22;
    const int gap = 3;
    const int x = 8;
    const int y0 = 30;

    for (int i = 0; i < 7; i++) {
        g_appsButtons[i] = draw_app_button(x, y0 + i * (h + gap), w, h, labels[i]);
    }
}

static void draw_chat_screen() {
    draw_header("Chat");

    int chatMaxH = g_keyboardVisible ? 86 : 170;
    int actionY = g_keyboardVisible ? 128 : 200;

    draw_chat_body(chatMaxH);

    if (g_keyboardVisible) {
        tft.fillRect(0, 116, tft.width(), 12, ILI9341_BLACK);
        display_draw_text(8, 117, "> " + g_chatInput, ILI9341_GREEN, 1);
        draw_chat_action_bar(actionY);
        draw_keyboard();
    } else {
        draw_chat_action_bar(actionY);
    }
}

static void draw_terminal_screen() {
    draw_header("Terminal");

    tft.fillRect(0, 26, tft.width(), 92, ILI9341_BLACK);
    draw_bounded_ascii(8, 30, 312, 86, g_termLog, ILI9341_GREEN);

    tft.fillRect(0, 118, tft.width(), 14, ILI9341_BLACK);
    display_draw_text(8, 120, "> " + g_termInput, ILI9341_GREEN, 1);

    draw_keyboard();
}

static void draw_settings_screen() {
    draw_header("Settings");

    String ip = wifi_is_ap_mode() ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    display_draw_text(8, 30, "WiFi: " + ip, UI_COLOR_OK, 1);
    display_draw_text(8, 44, "Provider: " + llm_get_provider_name(), UI_COLOR_TEXT, 1);

    const int w = 300;
    const int h = 18;
    const int x = 10;
    const int y0 = 56;
    const int gap = 1;

    g_settingsButtons[0] = draw_app_button(x, y0, w, h, "Calibrate 4-Point");
    g_settingsButtons[1] = draw_app_button(x, y0 + (h + gap), w, h, "Reset Calibration");
    g_settingsButtons[2] = draw_app_button(x, y0 + 2 * (h + gap), w, h, "Mic Test");
    g_settingsButtons[3] = draw_app_button(x, y0 + 3 * (h + gap), w, h, "Voice Chat");
    g_settingsButtons[4] = draw_app_button(x, y0 + 4 * (h + gap), w, h, "Start WiFi AP");
    g_settingsButtons[5] = draw_app_button(x, y0 + 5 * (h + gap), w, h, "Next Provider");
    g_settingsButtons[6] = draw_app_button(x, y0 + 6 * (h + gap), w, h, "System Info");
    g_settingsButtons[7] = draw_app_button(x, y0 + 7 * (h + gap), w, h, "Continuity");
}

static void refresh_wifi_networks() {
    if (g_wifiNetworkCount > 0 && millis() - g_wifiScanAt < 5000) {
        return;
    }
    g_wifiNetworkCount = WiFi.scanNetworks();
    if (g_wifiNetworkCount > 16) g_wifiNetworkCount = 16;
    if (g_wifiNetworkCount < 0) g_wifiNetworkCount = 0;

    for (int i = 0; i < g_wifiNetworkCount; i++) {
        String auth = "?";
        switch (WiFi.encryptionType(i)) {
            case WIFI_AUTH_OPEN: auth = "OPEN"; break;
            case WIFI_AUTH_WPA_PSK: auth = "WPA"; break;
            case WIFI_AUTH_WPA2_PSK: auth = "WPA2"; break;
            case WIFI_AUTH_WPA_WPA2_PSK: auth = "WPA/WPA2"; break;
            case WIFI_AUTH_WPA3_PSK: auth = "WPA3"; break;
            case WIFI_AUTH_WPA2_WPA3_PSK: auth = "WPA2/3"; break;
        }
        g_wifiDetails[i].ssid = WiFi.SSID(i);
        g_wifiDetails[i].auth = auth;
        g_wifiDetails[i].rssi = WiFi.RSSI(i);
        g_wifiDetails[i].channel = WiFi.channel(i);
    }

    for (int i = 0; i < g_wifiNetworkCount - 1; i++) {
        for (int j = i + 1; j < g_wifiNetworkCount; j++) {
            if (g_wifiDetails[j].rssi > g_wifiDetails[i].rssi) {
                WifiNet t = g_wifiDetails[i];
                g_wifiDetails[i] = g_wifiDetails[j];
                g_wifiDetails[j] = t;
            }
        }
    }

    for (int i = 0; i < g_wifiNetworkCount; i++) {
        g_wifiNetworks[i] = g_wifiDetails[i].ssid + " [" + g_wifiDetails[i].auth +
                            " " + String(g_wifiDetails[i].rssi) + "]";
    }
    WiFi.scanDelete();
    g_wifiScanAt = millis();
}

static void draw_wifi_screen() {
    draw_header("WiFi");
    refresh_wifi_networks();

    String mode = wifi_is_ap_mode() ? "AP" : "STA";
    display_draw_text(8, 28, "Mode: " + mode + "  IP: " + wifi_get_ip_display(), ILI9341_GREEN, 1);
    display_draw_text(8, 40, "Current: " + wifi_get_ssid_display(), ILI9341_CYAN, 1);
    display_draw_text(8, 52, "Networks (tap to connect):", ILI9341_GREEN, 1);

    if (g_wifiScroll > g_wifiNetworkCount) g_wifiScroll = 0;
    for (int i = 0; i < 5; i++) {
        int idx = g_wifiScroll + i;
        if (idx < g_wifiNetworkCount) {
            String label = g_wifiNetworks[idx];
            if (label.length() > 34) label = label.substring(0, 34);
            g_wifiNetworkButtons[i] = draw_app_button(8, 64 + i * 20, 304, 18, label);
        }
    }

    g_wifiNav[0] = draw_app_button(8, 166, 148, 18, "Prev");
    g_wifiNav[1] = draw_app_button(164, 166, 148, 18, "Next");
    g_wifiButtons[0] = draw_app_button(8, 190, 148, 20, "Start AP");
    g_wifiButtons[1] = draw_app_button(164, 190, 148, 20, "History");
    g_wifiButtons[2] = draw_app_button(8, 214, 148, 20, "Clear Creds");
    g_wifiButtons[3] = draw_app_button(164, 214, 148, 20, "Reboot");
}

static void connect_wifi_selected() {
    if (g_wifiSelectedSsid.length() == 0) return;
    wifi_save_credentials(g_wifiSelectedSsid, g_wifiPassInput);
    tft.fillScreen(ILI9341_BLACK);
    display_draw_text(20, 80, "Saving... Rebooting", ILI9341_GREEN, 2);
    delay(500);
    ESP.restart();
}

static void draw_wifi_connect_screen() {
    draw_header("Connect WiFi");

    display_draw_text(8, 30, "SSID: " + g_wifiSelectedSsid, ILI9341_GREEN, 1);
    tft.fillRect(0, 116, tft.width(), 12, ILI9341_BLACK);
    display_draw_text(8, 117, "Pass: " + g_wifiPassInput, ILI9341_GREEN, 1);

    g_wifiConnectActions[0] = draw_app_button(8, 128, 148, 20, "Connect");
    g_wifiConnectActions[1] = draw_app_button(164, 128, 148, 20, "Clear");
    draw_keyboard();
}

static void draw_wifi_history_screen() {
    draw_header("WiFi History");

    int n = wifi_history_count();
    if (n == 0) {
        display_draw_text(8, 40, "No saved networks", ILI9341_GREEN, 1);
        return;
    }

    int y = 30;
    for (int i = 0; i < n && y < 220; i++) {
        String line = String(i + 1) + ". " + wifi_history_ssid(i) +
                      " / " + mask_pass(wifi_history_pass(i));
        display_draw_text(8, y, line, ILI9341_GREEN, 1);
        y += 20;
    }
}

static void refresh_ble_devices() {
    if (g_bleCount > 0 && millis() - g_bleScanAt < 10000) {
        return;
    }
    g_bleCount = ble_scan_devices(g_bleDevices, BLE_SCAN_MAX);
    g_bleScanAt = millis();
}

static void draw_ble_screen() {
    draw_header("BLE Scan");
    refresh_ble_devices();

    display_draw_text(8, 28, "Nearby BLE devices:", ILI9341_GREEN, 1);
    for (int i = 0; i < 6; i++) {
        if (i < g_bleCount) {
            String label = g_bleDevices[i].name.length() > 0 ? g_bleDevices[i].name : g_bleDevices[i].address;
            label += " " + String(g_bleDevices[i].rssi) + "dBm";
            if (label.length() > 34) label = label.substring(0, 34);
            g_bleButtons[i] = draw_app_button(8, 42 + i * 22, 304, 20, label);
        }
    }
    if (g_bleCount == 0) {
        display_draw_text(8, 54, "No BLE devices found", ILI9341_CYAN, 1);
    }

    g_bleRescan = draw_app_button(8, 210, 304, 22, "Rescan");
}

static void draw_logs_screen() {
    draw_header("Logs");
    g_lastLogText = logger_get_recent();
    update_logs_view();
}

static void update_logs_view() {
    tft.fillRect(0, 26, tft.width(), 206, ILI9341_BLACK);
    draw_bounded_ascii(8, 30, 312, 200, logger_get_recent(), ILI9341_GREEN);
}

static void draw_info_screen() {
    draw_header("Monitor");

    uint32_t secs = millis() / 1000;
    char up[16];
    snprintf(up, sizeof(up), "%02lu:%02lu:%02lu",
             (unsigned long)(secs / 3600),
             (unsigned long)((secs % 3600) / 60),
             (unsigned long)(secs % 60));

    uint32_t heapTotal = ESP.getHeapSize();
    uint32_t heapFree = ESP.getFreeHeap();
    uint32_t psramTotal = ESP.getPsramSize();
    uint32_t psramFree = ESP.getFreePsram();

    String lines[16];
    int n = 0;
    float vin = read_supply_voltage();
    Serial.println("[Monitor] voltage=" + String(vin, 3) + " V");
    lines[n++] = "WiFi: " + wifi_get_ssid_display() + " (" +
                 wifi_get_ip_display() + ")";
    lines[n++] = "Uptime: " + String(up);
    lines[n++] = "Voltage: " + (vin < 0 ? String("N/A") :
                 String(vin, 2) + " V (ADC pin " +
                 String(VOLT_SENSE_PIN) + ")");
    lines[n++] = "Temp: " + String((int)temperatureRead()) + "C  CPU: " +
                 String(ESP.getCpuFreqMHz()) + " MHz";
    lines[n++] = "Heap free: " + String(heapFree / 1024) + " / " +
                 String(heapTotal / 1024) + " KB";
    lines[n++] = "PSRAM free: " + String(psramFree / 1024) + " / " +
                 String(psramTotal / 1024) + " KB";
    lines[n++] = "Flash: " + String(ESP.getFlashChipSize() / 1024 / 1024) +
                 " MB  Sketch: " +
                 String(ESP.getSketchSize() / 1024) + " KB";
    lines[n++] = "RSSI: " + String(WiFi.RSSI()) + " dBm";
    lines[n++] = "MAC: " + WiFi.macAddress();
    lines[n++] = "Provider: " + llm_get_provider_name();
    lines[n++] = "Version: v1.0.0";
    if (g_monitorScroll < 0) g_monitorScroll = 0;
    if (g_monitorScroll > n - 10) g_monitorScroll = n - 10;
    if (g_monitorScroll < 0) g_monitorScroll = 0;

    int y = 28;
    for (int i = g_monitorScroll; i < n && y <= 184; i++) {
        display_draw_text(8, y, lines[i], ILI9341_GREEN, 1);
        y += 12;
    }
    if (g_monitorScroll > 0) {
        display_draw_text(280, 218, "^", ILI9341_CYAN, 1);
    }
    if (g_monitorScroll < n - 10) {
        display_draw_text(296, 218, "v", ILI9341_CYAN, 1);
    }
    g_monitorNav[0] = draw_app_button(8, 194, 148, 22, "Up");
    g_monitorNav[1] = draw_app_button(164, 194, 148, 22, "Down");
}

static String weather_temp(float v) {
    if (v < -900) return "N/A";
    return String(v, 1);
}

static void draw_weather_cloud(int x, int y, uint16_t color) {
    tft.fillCircle(x - 18, y, 12, color);
    tft.fillCircle(x + 4, y - 5, 16, color);
    tft.fillCircle(x + 24, y, 10, color);
    tft.fillRect(x - 26, y, 58, 13, color);
    tft.fillRect(x - 26, y + 1, 58, 5, color);
}

static void draw_weather_sun(int cx, int cy, int frame) {
    tft.fillCircle(cx, cy, 13, ILI9341_YELLOW);
    tft.fillCircle(cx, cy, 9, 0xFFE0);
    for (int i = 0; i < 8; i++) {
        float a = frame * 0.2f + i * 3.14159f / 4.0f;
        int r1 = 16;
        int r2 = 21;
        int x1 = cx + (int)(cosf(a) * r1);
        int y1 = cy + (int)(sinf(a) * r1);
        int x2 = cx + (int)(cosf(a) * r2);
        int y2 = cy + (int)(sinf(a) * r2);
        tft.drawLine(x1, y1, x2, y2, 0xFFE0);
    }
}

static void draw_weather_rain(int cx, int y, int frame, uint16_t color) {
    for (int i = 0; i < 5; i++) {
        int x = cx - 26 + i * 13 + (frame % 2) * 3;
        int yy = y + 10 + ((frame * 2 + i * 6) % 6);
        tft.drawLine(x, yy, x - 2, yy + 7, color);
    }
}

static void draw_weather_icon(int cx, int cy, int frame) {
    int code = -1;
    if (weather_has_data()) code = weather_get().code;
    if (code < 0) return;

    bool clear = (code == 0 || code == 1);
    bool partly = (code == 2);
    bool overcast = (code == 3 || code == 45 || code == 48);
    bool rain = (code >= 51 && code <= 57) || (code >= 61 && code <= 67) ||
                (code >= 80 && code <= 82);
    bool snow = (code >= 71 && code <= 77) || (code == 85 || code == 86);
    bool storm = (code >= 95 && code <= 99);

    tft.fillCircle(cx, cy + 4, 27, 0x18E3);
    if (clear) {
        draw_weather_sun(cx, cy, frame);
    } else if (partly) {
        draw_weather_sun(cx - 10, cy - 8, frame);
        draw_weather_cloud(cx + 10 + (frame % 2) * 3, cy + 5, 0xEF7D);
    } else if (overcast) {
        draw_weather_cloud(cx - 5, cy - 2, 0x7BEF);
        draw_weather_cloud(cx + 9 + (frame % 2) * 3, cy + 8, 0xCE79);
    } else if (rain) {
        draw_weather_cloud(cx, cy - 9, 0xCE79);
        draw_weather_rain(cx, cy - 10, frame, ILI9341_CYAN);
    } else if (snow) {
        draw_weather_cloud(cx, cy - 9, 0xCE79);
        for (int i = 0; i < 7; i++) {
            int x = cx - 26 + (i * 12) % 52;
            int yy = cy + 6 + ((frame * 2 + i * 5) % 9);
            tft.fillCircle(x, yy, 1, ILI9341_WHITE);
        }
    } else if (storm) {
        draw_weather_cloud(cx, cy - 9, 0x7BEF);
        if (frame % 4 < 2) {
            tft.fillTriangle(cx + 8, cy - 8, cx + 18, cy - 8,
                             cx + 10, cy + 7, 0xFFE0);
            tft.fillTriangle(cx + 10, cy + 3, cx + 20, cy + 3,
                             cx + 14, cy + 18, 0xFFE0);
        }
        draw_weather_rain(cx, cy - 10, frame, ILI9341_CYAN);
    } else {
        draw_weather_cloud(cx, cy, 0xCE79);
    }
}

static void draw_weather_icon_region(int frame) {
    const int x = 224;
    const int y = 30;
    const int w = 84;
    const int h = 84;
    tft.fillRect(x, y, w, h, ILI9341_BLACK);
    draw_weather_icon(x + w / 2, y + h / 2, frame);
}

static void draw_weather_screen() {
    draw_header("Weather");

    if (!weather_refresh_if_stale()) {
        WeatherData d = weather_get();
        display_draw_text(8, 30, "Weather unavailable", ILI9341_RED, 2);
        display_draw_text(8, 52, d.error, ILI9341_YELLOW, 1);
    } else {
        WeatherData d = weather_get();
        String lines[14];
        int n = 0;
        lines[n++] = d.city;
        lines[n++] = weather_temp(d.temp) + "C  " + weather_code_text(d.code);
        lines[n++] = "Feels: " + weather_temp(d.feels) + "C";
        lines[n++] = "Humidity: " + String(d.humidity, 0) + "%";
        lines[n++] = "Rain: " + String(d.precip, 1) + "mm";
        lines[n++] = "Wind: " + String(d.wind, 0) + " km/h " +
                     weather_wind_dir(d.windDir);
        lines[n++] = "Pressure: " + String(d.pressure, 0) + " hPa";
        for (int i = 0; i < 3; i++) {
            lines[n++] = "D" + String(i + 1) + ": " +
                         weather_code_text(d.days[i].code);
            lines[n++] = "  Max " + String(d.days[i].maxTemp, 0) +
                         "C  Min " + String(d.days[i].minTemp, 0) +
                         "C";
            lines[n++] = "  Rain " + String(d.days[i].pop, 0) +
                         "%  Wind " + String(d.days[i].wind, 0);
        }
        if (g_weatherScroll < 0) g_weatherScroll = 0;
        if (g_weatherScroll > n - 11) g_weatherScroll = n - 11;
        if (g_weatherScroll < 0) g_weatherScroll = 0;
        int y = 28;
        bool firstLine = true;
        for (int i = g_weatherScroll; i < n && y <= 170; i++) {
            String t = lines[i];
            if (t.length() > 32) t = t.substring(0, 32);
            if (firstLine && g_weatherScroll == 0) {
                display_draw_text(8, y, t, ILI9341_GREEN, 3);
                y += 30;
            } else {
                display_draw_text(8, y, t, ILI9341_GREEN, 1);
                y += 12;
            }
            firstLine = false;
        }
        draw_weather_icon_region(g_weatherAnim);
        g_weatherButtons[0] = draw_app_button(8, 194, 100, 22, "Refresh");
        g_monitorNav[0] = draw_app_button(116, 194, 96, 22, "Up");
        g_monitorNav[1] = draw_app_button(220, 194, 92, 22, "Down");
        if (g_weatherScroll > 0) display_draw_text(296, 174, "^", ILI9341_CYAN, 1);
        if (g_weatherScroll < n - 11) display_draw_text(308, 174, "v", ILI9341_CYAN, 1);
        g_lastWeatherCheck = millis();
        return;
    }

    g_weatherButtons[0] = draw_app_button(8, 194, 304, 22, "Refresh");
    g_lastWeatherCheck = millis();
}

static uint16_t wifi_rssi_color(int rssi) {
    if (rssi >= -50) return ILI9341_GREEN;
    if (rssi >= -65) return ILI9341_YELLOW;
    if (rssi >= -75) return 0xFD20;
    return ILI9341_RED;
}

static void draw_wifi_analyzer_screen() {
    draw_header("WiFi Analyzer");
    refresh_wifi_networks();

    String mode = wifi_is_ap_mode() ? "AP" : "STA";
    display_draw_text(8, 28, "IP: " + wifi_get_ip_display() + "  " +
                      wifi_get_ssid_display(), ILI9341_GREEN, 1);

    if (g_wifiMapScroll > g_wifiNetworkCount) g_wifiMapScroll = 0;
    if (g_wifiMapScroll < 0) g_wifiMapScroll = 0;

    int visible = 6;
    for (int i = 0; i < visible; i++) {
        int idx = g_wifiMapScroll + i;
        if (idx >= g_wifiNetworkCount) break;

        int y = 42 + i * 23;
        String ssid = g_wifiDetails[idx].ssid;
        if (ssid.length() > 14) ssid = ssid.substring(0, 14);
        uint16_t color = wifi_rssi_color(g_wifiDetails[idx].rssi);

        display_draw_text(8, y, ssid, color, 1);
        String info = "ch" + String(g_wifiDetails[idx].channel) + " " +
                      String(g_wifiDetails[idx].rssi) + "dBm";
        display_draw_text(168, y, info, color, 1);

        int barW = (g_wifiDetails[idx].rssi + 100) * 304 / 70;
        if (barW < 2) barW = 2;
        if (barW > 304) barW = 304;
        tft.fillRect(8, y + 11, 304, 5, ILI9341_BLACK);
        tft.fillRect(8, y + 11, barW, 5, color);
    }

    if (g_wifiNetworkCount == 0) {
        display_draw_text(8, 54, "No networks found", ILI9341_CYAN, 1);
    }

    g_wifiMapNav[0] = draw_app_button(8, 194, 148, 22, "Prev");
    g_wifiMapNav[1] = draw_app_button(164, 194, 148, 22, "Next");
}

static void draw_music_screen() {
    draw_header("Music");

    int count = music_get_count();
    if (count == 0) {
        display_draw_text(8, 40, "No tracks found", ILI9341_CYAN, 1);
        display_draw_text(8, 54, "Add data/music WAV or data/cloud.txt", ILI9341_YELLOW, 1);
    }

    if (g_musicScroll > count - 6) g_musicScroll = count - 6;
    if (g_musicScroll < 0) g_musicScroll = 0;

    for (int i = 0; i < 6; i++) {
        int idx = g_musicScroll + i;
        if (idx < count) {
            String label = music_get_name(idx);
            bool playing = music_is_playing() && label == music_get_current_name();
            if (music_is_cloud(idx)) label = "[C] " + label;
            if (playing) {
                label = "> " + label;
            }
            if (label.length() > 30) label = label.substring(0, 30);
            g_musicButtons[i] = draw_app_button(8, 30 + i * 22, 304, 20, label);
        }
    }

    String status = music_is_playing()
        ? "Playing: " + music_get_current_name()
        : "Stopped";
    display_draw_text(8, 170, status, ILI9341_GREEN, 1);
    display_draw_text(220, 170, "Vol: " + String(audio_get_volume()), ILI9341_CYAN, 1);

    const int aw = 50;
    const int ag = 2;
    const int ax = 8;
    g_musicActions[0] = draw_app_button(ax, 194, aw, 22, "Vol-");
    g_musicActions[1] = draw_app_button(ax + (aw + ag), 194, aw, 22, "Prev");
    g_musicActions[2] = draw_app_button(ax + 2 * (aw + ag), 194, aw, 22, "Stop");
    g_musicActions[3] = draw_app_button(ax + 3 * (aw + ag), 194, aw, 22, "Next");
    g_musicActions[4] = draw_app_button(ax + 4 * (aw + ag), 194, aw, 22, "Vol+");
    g_musicActions[5] = draw_app_button(ax + 5 * (aw + ag), 194, aw, 22, "Refresh");
}

static void draw_tetris_screen() {
    draw_header("Tetris");
    if (!tetris_is_active()) tetris_start();
    tetris_draw();
}

static void draw_continuity_status() {
    bool ok = digitalRead(PROBE_B_PIN) == LOW;
    uint16_t color = ok ? ILI9341_GREEN : ILI9341_RED;
    tft.fillRect(8, 76, 304, 56, ILI9341_BLACK);
    display_draw_text(8, 80, ok ? "OK  (Closed)" : "OPEN", color, 3);
    display_draw_text(8, 104, "Connect wire: PROBE A <-> PROBE B",
                      ILI9341_GREEN, 1);
}

static void draw_continuity_screen() {
    draw_header("Continuity");
    display_draw_text(8, 34, "GPIO1 -> Probe A   GPIO2 -> Probe B",
                      ILI9341_CYAN, 1);
    display_draw_text(8, 48, "A=low output  B=pull-up input",
                      ILI9341_CYAN, 1);
    float vin = read_supply_voltage();
    display_draw_text(8, 60, "VIN: " + (vin < 0 ? String("N/A") :
                      String(vin, 2) + " V"), ILI9341_CYAN, 1);
    draw_continuity_status();
    g_continuityButtons[0] = draw_app_button(
        8, 180, 304, 22, g_contBeep ? "Beep: ON" : "Beep: OFF");
}

static void update_continuity_poll() {
    if (g_screen != SCREEN_CONTINUITY) return;
    static uint32_t lastPoll = 0;
    uint32_t now = millis();
    if (now - lastPoll < 150) return;
    lastPoll = now;
    bool ok = digitalRead(PROBE_B_PIN) == LOW;
    if (ok != g_contLastOk) {
        g_contLastOk = ok;
        if (g_contBeep) {
            audio_beep(ok ? 2200 : 400, ok ? 120 : 250);
        }
        draw_continuity_status();
    }
}

static void append_terminal(const String& text) {
    g_termLog += text;
    trim_log(g_termLog, 1800);
}

static String cmd_ipconfig() {
    String out;
    out += "SSID: " + wifi_get_ssid_display() + "\n";
    out += "IP: " + wifi_get_ip_display() + "\n";
    if (wifi_is_ap_mode()) {
        out += "Gateway: " + WiFi.softAPIP().toString() + "\n";
    } else {
        out += "Gateway: " + WiFi.gatewayIP().toString() + "\n";
        out += "Mask: " + WiFi.subnetMask().toString() + "\n";
        out += "DNS: " + WiFi.dnsIP().toString() + "\n";
    }
    out += "MAC: " + WiFi.macAddress() + "\n";
    return out;
}

static String cmd_ping(const String& rawHost) {
    String host = rawHost;
    host.trim();
    if (host.startsWith("http://")) host = host.substring(7);
    if (host.startsWith("https://")) host = host.substring(8);
    int slash = host.indexOf('/');
    if (slash >= 0) host = host.substring(0, slash);

    if (host.length() == 0) {
        return "Usage: ping <host>\n";
    }

    IPAddress ip;
    if (!WiFi.hostByName(host.c_str(), ip)) {
        return "Ping request could not find host " + host + "\n";
    }

    int ports[2] = {80, 443};
    for (int i = 0; i < 2; i++) {
        WiFiClient client;
        client.setTimeout(3000);
        uint32_t t0 = millis();
        if (client.connect(ip, ports[i])) {
            uint32_t ms = millis() - t0;
            client.stop();
            return "Reply from " + ip.toString() + ": time=" + String(ms) +
                   "ms (TCP port " + String(ports[i]) + ")\n";
        }
        client.stop();
    }

    return "Reply from " + ip.toString() + ": Destination host unreachable\n";
}

static void run_terminal_command() {
    String cmd = g_termInput;
    g_termInput = "";
    append_terminal("> " + cmd + "\n");

    String lower = cmd;
    lower.toLowerCase();
    String out;

    if (lower == "help") {
        out = "help | clear | cls | ipconfig | ping <host> | weather | music | cloud | play <n> | stop | tts stop | vol <0-100> | vol+ | vol- | say <text> | provider | calib | wifi | record | voice | continuity | ota <https-url> | reboot\n";
    } else if (lower == "clear") {
        g_termLog = "";
        draw_terminal_screen();
        return;
    } else if (lower == "cls") {
        g_termLog = "";
        draw_terminal_screen();
        return;
    } else if (lower == "ipconfig") {
        out = cmd_ipconfig();
    } else if (lower.startsWith("ping ")) {
        out = cmd_ping(cmd.substring(5));
    } else if (lower == "weather") {
        if (weather_fetch()) {
            WeatherData d = weather_get();
            out = d.city + " " + String(d.temp, 1) + "C " +
                  weather_code_text(d.code) + "\n";
            out += "Rain " + String(d.precip, 1) + "mm  Wind " +
                   String(d.wind, 0) + "km/h " +
                   weather_wind_dir(d.windDir) + "\n";
        } else {
            out = "Weather error: " + weather_get().error + "\n";
        }
    } else if (lower == "cloud" || lower == "cloud list") {
        int n = music_get_cloud_count();
        if (n == 0) {
            out = "No cloud tracks in /cloud.txt\n";
        } else {
            out = "Cloud tracks:\n";
            int base = music_get_count() - n;
            for (int i = 0; i < n; i++) {
                out += String(base + i) + ". " + music_get_cloud_name(i) +
                       "\n  " + music_get_cloud_url(i) + "\n";
            }
        }
    } else if (lower.startsWith("cloud play ")) {
        int n = cmd.substring(11).toInt();
        int base = music_get_count() - music_get_cloud_count();
        int idx = base + n;
        out = music_play(idx)
            ? ("Playing cloud " + music_get_name(idx) + "\n")
            : "Cannot play cloud track " + String(n) + "\n";
    } else if (lower.startsWith("play ")) {
        int idx = cmd.substring(5).toInt();
        out = music_play(idx)
            ? ("Playing " + music_get_name(idx) + "\n")
            : "Cannot play file\n";
    } else if (lower == "stop") {
        music_stop();
        out = "Music stopped\n";
    } else if (lower == "vol+" || lower == "vol up") {
        audio_set_volume(audio_get_volume() + 10);
        out = "Volume " + String(audio_get_volume()) + "\n";
    } else if (lower == "vol-" || lower == "vol down") {
        audio_set_volume(audio_get_volume() - 10);
        out = "Volume " + String(audio_get_volume()) + "\n";
    } else if (lower.startsWith("vol ")) {
        int v = cmd.substring(4).toInt();
        audio_set_volume(v);
        out = "Volume " + String(audio_get_volume()) + "\n";
    } else if (lower == "vol") {
        out = "Volume " + String(audio_get_volume()) +
              " (vol <0-100> | vol+ | vol-)\n";
    } else if (lower.startsWith("tone ")) {
        int freq = cmd.substring(5).toInt();
        if (freq < 100 || freq > 5000) freq = 1000;
        if (!audio_beep(freq, 1200)) {
            out = "Audio output not initialized\n";
        } else {
            out = "Tone " + String(freq) + "Hz played\n";
        }
    } else if (lower == "beep") {
        if (!audio_beep(1000, 1200)) {
            out = "Audio output not initialized\n";
        } else {
            out = "Beep played\n";
        }
    } else if (lower.startsWith("say ")) {
        String text = cmd.substring(4);
        text.trim();
        if (text.length() == 0) {
            out = "Usage: say <text>\n";
        } else {
            append_terminal("Speaking...\n");
            draw_terminal_screen();
            out = tts_speak(text) ? "Speech started\n" : "TTS failed\n";
        }
    } else if (lower == "tts stop" || lower == "stop say" || lower == "stopsay") {
        tts_stop();
        out = "TTS stop requested\n";
    } else if (lower == "provider") {
        out = provider_list_text();
    } else if (lower.startsWith("provider ")) {
        String arg = cmd.substring(9);
        arg.trim();
        int idx = arg.toInt();
        bool ok = (idx > 0 || arg == "0") ? llm_set_provider(idx) : llm_set_provider_by_name(arg);
        out = ok ? ("Switched to " + llm_get_provider_name() + "\n") : "Provider not found\n";
    } else if (lower == "calib show") {
        out = "X: " + String(g_touchCalib.xMin) + "-" + String(g_touchCalib.xMax) +
              " Y: " + String(g_touchCalib.yMin) + "-" + String(g_touchCalib.yMax) +
              " swap=" + String(g_touchCalib.swapXY) +
              " invX=" + String(g_touchCalib.invertX) +
              " invY=" + String(g_touchCalib.invertY) + "\n";
    } else if (lower == "calib") {
        append_terminal("Central 4-point calibration...\n");
        draw_terminal_screen();
        touch_calib_run_interactive();
        out = "Calibration done\n";
    } else if (lower == "wifi") {
        out = "Mode: " + String(wifi_is_ap_mode() ? "AP" : "STA") +
              " IP: " + wifi_get_ip_display() +
              " Saved: " + wifi_get_saved_ssid() + "\n";
    } else if (lower == "wifi history") {
        int n = wifi_history_count();
        if (n == 0) {
            out = "No saved networks\n";
        } else {
            for (int i = 0; i < n; i++) {
                out += String(i + 1) + ". " + wifi_history_ssid(i) +
                       " / " + mask_pass(wifi_history_pass(i)) + "\n";
            }
        }
    } else if (lower.startsWith("ota ")) {
        String url = cmd.substring(4);
        url.trim();
        out = ota_start_from_url(url)
            ? "OTA download started\n"
            : "OTA start failed (use https:// URL)\n";
    } else if (lower == "ota") {
        out = "Usage: ota https://host/firmware.bin\n";
    } else if (lower.startsWith("wifi set ")) {
        String rest = cmd.substring(9);
        int sp = rest.indexOf(' ');
        if (sp <= 0) {
            out = "Usage: wifi set <ssid> <password>\n";
        } else {
            String ssid = rest.substring(0, sp);
            String pass = rest.substring(sp + 1);
            wifi_save_credentials(ssid, pass);
            append_terminal("Saved. Rebooting...\n");
            draw_terminal_screen();
            delay(300);
            ESP.restart();
        }
    } else if (lower == "record") {
        Serial.println("[Audio] Recording 3s...");
        append_terminal("Recording 3s...\n");
        draw_terminal_screen();
        music_stop();
        uint32_t stopAt = millis();
        while (music_is_playing() && millis() - stopAt < 1500) delay(5);
        int16_t* samples = NULL;
        size_t count = audio_mic_record(3000, samples);
        if (count > 0) {
            Serial.println("[Audio] Recorded " + String(count) + " samples, playing...");
            append_terminal("Recorded " + String(count) + " samples, playing...\n");
            draw_terminal_screen();
            audio_play_pcm(samples, count);
            heap_caps_free(samples);
            Serial.println("[Audio] Playback done");
            out = "Playback done\n";
        } else {
            Serial.println("[Audio] Mic error or no microphone");
            out = "Mic error or no microphone connected\n";
        }
    } else if (lower == "reboot") {
        append_terminal("Rebooting...\n");
        draw_terminal_screen();
        delay(300);
        ESP.restart();
    } else {
        out = "Unknown command: " + cmd + "\n";
    }

    append_terminal(out);
    draw_terminal_screen();
}

static void send_chat() {
    if (g_llmBusy) return;
    String prompt = g_chatInput;
    prompt.trim();
    if (prompt.length() == 0) return;

    String context = g_chatLog;
    trim_log(context, 2000);

    g_chatInput = "";
    g_chatLog += "You: " + prompt + "\n";
    trim_log(g_chatLog, 8000);
    draw_chat_screen();

    g_llmBusy = true;
    tft.fillRect(0, 118, tft.width(), 14, ILI9341_BLACK);
    display_draw_text(8, 120, "Thinking...", UI_COLOR_WARN, 1);

    LLMResult r = llm_chat_with_context(prompt, context);
    if (r.ok) {
        g_chatLog += "AI: " + extract_ai_display(r.content) + "\n";
        g_lastAiReply = extract_ai_tts(r.content);
        audio_ok_sound();
    } else {
        g_chatLog += "Error: " + r.errorMsg + "\n";
        audio_error_sound();
    }
    trim_log(g_chatLog, 8000);
    g_llmBusy = false;
    draw_chat_screen();
}

static String local_device_intent(const String& q) {
    String s = q;
    s.toLowerCase();

    if (s.indexOf("weather") >= 0 || s.indexOf("天气") >= 0 ||
        s.indexOf("温度") >= 0 || s.indexOf("下雨") >= 0 ||
        s.indexOf("风力") >= 0) {
        if (!weather_refresh_if_stale()) {
            return "Weather unavailable: " + weather_get().error;
        }
        WeatherData d = weather_get();
        return String("Weather in ") + d.city + ": " +
               weather_temp(d.temp) + "C, " +
               weather_code_text(d.code) + ", humidity " +
               String(d.humidity, 0) + "%, rain " +
               String(d.precip, 1) + "mm, wind " +
               String(d.wind, 0) + " km/h";
    }
    if (s.indexOf("wifi") >= 0 || s.indexOf("wi-fi") >= 0 ||
        s.indexOf("ssid") >= 0 || s.indexOf("网络") >= 0 ||
        s.indexOf("网速") >= 0 || s.indexOf("ip") >= 0) {
        String out = wifi_is_ap_mode()
            ? String("WiFi AP mode, SSID ESP32-AI-Setup, IP ") + wifi_get_ip_display()
            : String("WiFi connected: ") + wifi_get_ssid_display() +
              ", IP " + wifi_get_ip_display() +
              ", RSSI " + String(WiFi.RSSI()) + " dBm";
        return out;
    }
    if (s.indexOf("uptime") >= 0 || s.indexOf("运行时间") >= 0 ||
        s.indexOf("开机多久") >= 0) {
        uint32_t sec = millis() / 1000;
        return "Uptime: " + String(sec / 3600) + "h " +
               String((sec % 3600) / 60) + "m " +
               String(sec % 60) + "s";
    }
    if (s.indexOf("memory") >= 0 || s.indexOf("ram") >= 0 ||
        s.indexOf("内存") >= 0) {
        return "Free heap: " + String(ESP.getFreeHeap() / 1024) +
               " KB, PSRAM free: " +
               String(ESP.getFreePsram() / 1024) + " KB";
    }
    if (s.indexOf("time") >= 0 || s.indexOf("几点") >= 0 ||
        s.indexOf("时间") >= 0) {
        return "Current time: " + format_hhmmss();
    }
    if (s.indexOf("volume") >= 0 || s.indexOf("音量") >= 0) {
        return "Current volume: " + String(audio_get_volume()) +
               " (0-100)";
    }
    return "";
}

static void run_voice_chat() {
    if (g_llmBusy || tts_is_busy()) return;
    g_voiceActive = true;
    if (g_screen == SCREEN_CHAT) {
        draw_chat_action_bar(g_keyboardVisible ? 128 : 200);
    }
    music_stop();
    uint32_t stopAt = millis();
    while (music_is_playing() && millis() - stopAt < 1500) delay(5);

    tft.fillRect(0, 118, tft.width(), 14, ILI9341_BLACK);
    display_draw_text(8, 120, "Listening...", UI_COLOR_WARN, 1);
    String transcript = voice_start_chat();
    Serial.println("[Voice] result: " + transcript);
    if (transcript.startsWith("VOICE_ERR:")) {
        String msg = transcript.substring(10);
        g_chatLog += "Voice error: " + msg + "\n";
        trim_log(g_chatLog, 8000);
        tft.fillRect(0, 118, tft.width(), 14, ILI9341_BLACK);
        display_draw_text(8, 120, "Voice failed", UI_COLOR_ERR, 1);
        delay(800);
        g_voiceActive = false;
        draw_chat_screen();
        return;
    }

    g_chatLog += "You (voice): " + transcript + "\n";
    trim_log(g_chatLog, 8000);
    tft.fillRect(0, 118, tft.width(), 14, ILI9341_BLACK);
    display_draw_text(8, 120, "Thinking...", UI_COLOR_WARN, 1);

    String local = local_device_intent(transcript);
    if (local.length() > 0) {
        g_chatLog += "AI: " + local + "\n";
        trim_log(g_chatLog, 8000);
        tft.fillRect(0, 118, tft.width(), 14, ILI9341_BLACK);
        display_draw_text(8, 120, "Speaking...", UI_COLOR_WARN, 1);
        tts_speak(local);
        g_voiceActive = false;
        draw_chat_screen();
        return;
    }

    g_llmBusy = true;
    String context = g_chatLog;
    trim_log(context, 2000);
    LLMResult r = llm_chat_with_context(transcript, context);
    g_llmBusy = false;
    if (r.ok) {
        g_chatLog += "AI: " + extract_ai_display(r.content) + "\n";
        g_lastAiReply = extract_ai_tts(r.content);
    } else {
        g_chatLog += "Error: " + r.errorMsg + "\n";
    }
    trim_log(g_chatLog, 8000);
    if (g_lastAiReply.length() > 0) {
        tft.fillRect(0, 118, tft.width(), 14, ILI9341_BLACK);
        display_draw_text(8, 120, "Speaking...", UI_COLOR_WARN, 1);
        tts_speak(g_lastAiReply);
    }
    g_voiceActive = false;
    draw_chat_screen();
}

static void handle_keyboard_touch(Button* hit) {
    if (!hit) return;

    String* input = (g_screen == SCREEN_CHAT) ? &g_chatInput :
                    (g_screen == SCREEN_WIFI_CONNECT) ? &g_wifiPassInput : &g_termInput;
    if (hit->label == "<-") {
        if (input->length() > 0) {
            input->remove(input->length() - 1);
        }
    } else if (hit->label == "Space") {
        *input += ' ';
    } else if (hit->label == "Send") {
        if (g_screen == SCREEN_CHAT) {
            send_chat();
        } else if (g_screen == SCREEN_TERMINAL) {
            run_terminal_command();
        } else if (g_screen == SCREEN_WIFI_CONNECT) {
            connect_wifi_selected();
        }
        return;
    } else {
        *input += hit->label;
    }

    // 只刷新输入行, 避免每次按键都重画整个键盘造成明显卡顿
    if (g_screen == SCREEN_CHAT) {
        tft.fillRect(0, 116, tft.width(), 12, ILI9341_BLACK);
        display_draw_text(8, 117, "> " + g_chatInput, ILI9341_GREEN, 1);
    } else if (g_screen == SCREEN_WIFI_CONNECT) {
        tft.fillRect(0, 116, tft.width(), 12, ILI9341_BLACK);
        display_draw_text(8, 117, "Pass: " + g_wifiPassInput, ILI9341_GREEN, 1);
    } else {
        tft.fillRect(0, 118, tft.width(), 14, ILI9341_BLACK);
        display_draw_text(8, 120, "> " + g_termInput, ILI9341_GREEN, 1);
    }
}

static void handle_touch_xy(int x, int y) {
    if (!music_is_playing() && !tts_is_busy()) audio_touch_sound();

    if (g_screen != SCREEN_HOME && hit_button(g_back, x, y)) {
        g_screen = SCREEN_HOME;
        draw_home_screen();
        touch_wait_release();
        return;
    }

    if (g_screen == SCREEN_HOME) {
        for (int i = 0; i < 6; i++) {
            if (hit_button(g_homeButtons[i], x, y)) {
                touch_wait_release();
                if (i == 0) { g_screen = SCREEN_CHAT; draw_chat_screen(); }
                else if (i == 1) { g_screen = SCREEN_TERMINAL; draw_terminal_screen(); }
                else if (i == 2) { g_screen = SCREEN_SETTINGS; draw_settings_screen(); }
                else if (i == 3) { g_screen = SCREEN_WIFI; g_wifiScroll = 0; draw_wifi_screen(); }
                else if (i == 4) { g_screen = SCREEN_APPS; draw_apps_screen(); }
                else { g_screen = SCREEN_INFO; draw_info_screen(); }
                return;
            }
        }
    } else if (g_screen == SCREEN_APPS) {
        for (int i = 0; i < 7; i++) {
            if (hit_button(g_appsButtons[i], x, y)) {
                touch_wait_release();
                if (i == 0) { g_screen = SCREEN_WEATHER; g_weatherScroll = 0; draw_weather_screen(); }
                else if (i == 1) { g_screen = SCREEN_WIFI_ANALYZER; g_wifiMapScroll = 0; draw_wifi_analyzer_screen(); }
                else if (i == 2) { g_screen = SCREEN_MUSIC; draw_music_screen(); }
                else if (i == 3) { g_screen = SCREEN_TETRIS; draw_tetris_screen(); }
                else if (i == 4) { g_screen = SCREEN_LOGS; draw_logs_screen(); }
                else if (i == 5) { g_screen = SCREEN_BLE; draw_ble_screen(); }
                else { g_screen = SCREEN_INFO; draw_info_screen(); }
                return;
            }
        }
    } else if (g_screen == SCREEN_CHAT) {
        if (y < 124) {
            int startY = y;
            int endY = startY;
            bool moved = false;
            uint32_t t0 = millis();
            while (millis() - t0 < 600) {
                TouchPoint n = touch_read();
                if (!n.pressed) break;
                if (abs(n.y - startY) > 8) {
                    moved = true;
                    endY = n.y;
                }
                delay(5);
            }
            if (moved) {
                int dy = endY - startY;
                if (dy < -15) g_chatScroll += 3;
                else if (dy > 15) g_chatScroll -= 3;
                draw_chat_screen();
            }
            return;
        }

        for (int i = 0; i < 6; i++) {
            if (hit_button(g_chatActions[i], x, y)) {
                touch_wait_release();
                if (i == 0) {
                    g_chatLog = "";
                    g_chatInput = "";
                    g_chatScroll = 0;
                    g_lastAiReply = "";
                } else if (i == 1) {
                    g_chatScroll -= 3;
                } else if (i == 2) {
                    g_chatScroll += 3;
                } else if (i == 3) {
                    g_keyboardVisible = !g_keyboardVisible;
                } else if (i == 4) {
                    if (g_lastAiReply.length() == 0) {
                        tft.fillRect(0, 118, tft.width(), 14, ILI9341_BLACK);
                        display_draw_text(8, 120, "No AI reply yet", UI_COLOR_WARN, 1);
                        delay(600);
                    } else {
                        Serial.println("[TTS] Speak len=" + String(g_lastAiReply.length()));
                        tft.fillRect(0, 118, tft.width(), 14, ILI9341_BLACK);
                        display_draw_text(8, 120, "Speaking...", UI_COLOR_WARN, 1);
                        bool ok = tts_speak(g_lastAiReply);
                        Serial.println(ok ? "[TTS] Speak done" : "[TTS] Speak failed");
                        if (!ok) {
                            tft.fillRect(0, 118, tft.width(), 14, ILI9341_BLACK);
                            display_draw_text(8, 120, "TTS failed", UI_COLOR_ERR, 1);
                            delay(800);
                        }
                    }
                } else {
                    if (g_voiceActive || tts_is_busy()) {
                        tts_stop();
                    } else {
                        run_voice_chat();
                    }
                }
                draw_chat_screen();
                return;
            }
        }

        for (int i = 0; i < g_keyCount; i++) {
            if (display_point_in_button(g_keys[i], x, y)) {
                touch_wait_release();
                handle_keyboard_touch(&g_keys[i]);
                return;
            }
        }
    } else if (g_screen == SCREEN_TERMINAL) {
        for (int i = 0; i < g_keyCount; i++) {
            if (display_point_in_button(g_keys[i], x, y)) {
                touch_wait_release();
                handle_keyboard_touch(&g_keys[i]);
                return;
            }
        }
    } else if (g_screen == SCREEN_SETTINGS) {
        for (int i = 0; i < 8; i++) {
            if (hit_button(g_settingsButtons[i], x, y)) {
                touch_wait_release();
                if (i == 0) {
                    touch_calib_run_interactive();
                    draw_settings_screen();
                } else if (i == 1) {
                    touch_calib_reset();
                    draw_settings_screen();
                } else if (i == 2) {
                    tft.fillRect(0, 226, tft.width(), 14, ILI9341_BLACK);
                    display_draw_text(8, 227, "Recording 3s...", UI_COLOR_WARN, 1);
                    music_stop();
                    uint32_t stopAt = millis();
                    while (music_is_playing() && millis() - stopAt < 1500) delay(5);
                    int16_t* samples = NULL;
                    size_t count = audio_mic_record(3000, samples);
                    if (count > 0) {
                        tft.fillRect(0, 226, tft.width(), 14, ILI9341_BLACK);
                        display_draw_text(8, 227, "Playing...", UI_COLOR_WARN, 1);
                        audio_play_pcm(samples, count);
                        heap_caps_free(samples);
                    } else {
                        tft.fillRect(0, 226, tft.width(), 14, ILI9341_BLACK);
                        display_draw_text(8, 227, "Mic error", UI_COLOR_ERR, 1);
                    }
                    delay(300);
                    draw_settings_screen();
                } else if (i == 3) {
                    run_voice_chat();
                    draw_settings_screen();
                } else if (i == 4) {
                    wifi_start_ap();
                    ble_provision_start();
                    web_server_start();
                    draw_wifi_screen();
                } else if (i == 5) {
                    llm_next_provider();
                    draw_settings_screen();
                } else if (i == 6) {
                    g_screen = SCREEN_INFO;
                    draw_info_screen();
                } else if (i == 7) {
                    g_screen = SCREEN_CONTINUITY;
                    draw_continuity_screen();
                } else {
                    g_screen = SCREEN_BLE;
                    draw_ble_screen();
                }
                return;
            }
        }
    } else if (g_screen == SCREEN_BLE) {
        if (hit_button(g_bleRescan, x, y)) {
            touch_wait_release();
            g_bleScanAt = 0;
            draw_ble_screen();
            return;
        }
    } else if (g_screen == SCREEN_WIFI) {
        for (int i = 0; i < 2; i++) {
            if (hit_button(g_wifiNav[i], x, y)) {
                touch_wait_release();
                if (i == 0) {
                    g_wifiScroll -= 5;
                    if (g_wifiScroll < 0) g_wifiScroll = 0;
                } else {
                    g_wifiScroll += 5;
                }
                draw_wifi_screen();
                return;
            }
        }

        for (int i = 0; i < 5; i++) {
            int idx = g_wifiScroll + i;
            if (idx < g_wifiNetworkCount &&
                hit_button(g_wifiNetworkButtons[i], x, y)) {
                touch_wait_release();
                int sep = g_wifiNetworks[idx].lastIndexOf(" [");
                g_wifiSelectedSsid = sep > 0 ? g_wifiNetworks[idx].substring(0, sep) : g_wifiNetworks[idx];
                bool isOpen = g_wifiNetworks[idx].indexOf("OPEN") >= 0;
                String savedPass = history_pass_for_ssid(g_wifiSelectedSsid);
                if (isOpen || savedPass.length() > 0) {
                    g_wifiPassInput = savedPass;
                    connect_wifi_selected();
                } else {
                    g_wifiPassInput = "";
                    g_screen = SCREEN_WIFI_CONNECT;
                    draw_wifi_connect_screen();
                }
                return;
            }
        }

        for (int i = 0; i < 4; i++) {
            if (hit_button(g_wifiButtons[i], x, y)) {
                touch_wait_release();
                if (i == 0) {
                    wifi_start_ap();
                    ble_provision_start();
                    web_server_start();
                    draw_wifi_screen();
                } else if (i == 1) {
                    g_screen = SCREEN_WIFI_HISTORY;
                    draw_wifi_history_screen();
                } else if (i == 2) {
                    wifi_clear_credentials();
                    draw_wifi_screen();
                } else {
                    ESP.restart();
                }
                return;
            }
        }
    } else if (g_screen == SCREEN_WIFI_CONNECT) {
        for (int i = 0; i < 2; i++) {
            if (hit_button(g_wifiConnectActions[i], x, y)) {
                touch_wait_release();
                if (i == 0) {
                    connect_wifi_selected();
                } else {
                    g_wifiPassInput = "";
                    draw_wifi_connect_screen();
                }
                return;
            }
        }

        for (int i = 0; i < g_keyCount; i++) {
            if (display_point_in_button(g_keys[i], x, y)) {
                touch_wait_release();
                handle_keyboard_touch(&g_keys[i]);
                return;
            }
        }
    } else if (g_screen == SCREEN_WEATHER) {
        if (hit_button(g_weatherButtons[0], x, y)) {
            touch_wait_release();
            draw_weather_screen();
            return;
        }
        if (hit_button(g_monitorNav[0], x, y)) {
            touch_wait_release();
            g_weatherScroll -= 5;
            draw_weather_screen();
            return;
        }
        if (hit_button(g_monitorNav[1], x, y)) {
            touch_wait_release();
            g_weatherScroll += 5;
            draw_weather_screen();
            return;
        }
    } else if (g_screen == SCREEN_INFO) {
        for (int i = 0; i < 2; i++) {
            if (hit_button(g_monitorNav[i], x, y)) {
                touch_wait_release();
                g_monitorScroll += (i == 0) ? -5 : 5;
                draw_info_screen();
                return;
            }
        }
    } else if (g_screen == SCREEN_CONTINUITY) {
        if (hit_button(g_continuityButtons[0], x, y)) {
            touch_wait_release();
            g_contBeep = !g_contBeep;
            draw_continuity_screen();
            return;
        }
    } else if (g_screen == SCREEN_WIFI_ANALYZER) {
        for (int i = 0; i < 2; i++) {
            if (hit_button(g_wifiMapNav[i], x, y)) {
                touch_wait_release();
                if (i == 0) {
                    g_wifiMapScroll -= 6;
                    if (g_wifiMapScroll < 0) g_wifiMapScroll = 0;
                } else {
                    g_wifiMapScroll += 6;
                }
                draw_wifi_analyzer_screen();
                return;
            }
        }
    } else if (g_screen == SCREEN_MUSIC) {
        for (int i = 0; i < 6; i++) {
            if (hit_button(g_musicActions[i], x, y)) {
                touch_wait_release();
                if (i == 0) {
                    audio_set_volume(audio_get_volume() - 10);
                } else if (i == 1) {
                    g_musicScroll -= 6;
                    if (g_musicScroll < 0) g_musicScroll = 0;
                } else if (i == 2) {
                    music_stop();
                } else if (i == 3) {
                    g_musicScroll += 6;
                } else if (i == 4) {
                    audio_set_volume(audio_get_volume() + 10);
                } else {
                    music_init();
                }
                draw_music_screen();
                return;
            }
        }
        for (int i = 0; i < 6; i++) {
            if (i < music_get_count() &&
                hit_button(g_musicButtons[i], x, y)) {
                touch_wait_release();
                music_play(g_musicScroll + i);
                draw_music_screen();
                return;
            }
        }
    } else if (g_screen == SCREEN_TETRIS) {
        if (tetris_handle_touch(x, y)) {
            touch_wait_release();
            return;
        }
    }

    touch_wait_release();
}

static void handle_touch() {
    TouchPoint tp = touch_read();
    if (!tp.pressed) return;
    handle_touch_xy(tp.x, tp.y);
}

static void process_serial_line(const String& line) {
    String input = line;
    input.trim();
    if (input.length() == 0) return;

    String lower = input;
    lower.toLowerCase();

    if (lower == "home") {
        g_screen = SCREEN_HOME;
        draw_home_screen();
        return;
    }
    if (lower == "apps") {
        g_screen = SCREEN_APPS;
        draw_apps_screen();
        return;
    }
    if (lower == "chat") {
        g_screen = SCREEN_CHAT;
        draw_chat_screen();
        return;
    }
    if (lower == "term" || lower == "terminal") {
        g_screen = SCREEN_TERMINAL;
        draw_terminal_screen();
        return;
    }
    if (lower == "settings") {
        g_screen = SCREEN_SETTINGS;
        draw_settings_screen();
        return;
    }
    if (lower == "logs") {
        g_screen = SCREEN_LOGS;
        draw_logs_screen();
        return;
    }
    if (lower == "monitor" || lower == "info") {
        g_screen = SCREEN_INFO;
        draw_info_screen();
        return;
    }
    if (lower == "wifi") {
        g_screen = SCREEN_WIFI;
        g_wifiScroll = 0;
        draw_wifi_screen();
        return;
    }
    if (lower == "weather") {
        g_screen = SCREEN_WEATHER;
        g_weatherScroll = 0;
        draw_weather_screen();
        return;
    }
    if (lower == "wifi map" || lower == "wifimap") {
        g_screen = SCREEN_WIFI_ANALYZER;
        g_wifiMapScroll = 0;
        draw_wifi_analyzer_screen();
        Serial.println("[WiFiMap] found " + String(g_wifiNetworkCount) + " networks");
        return;
    }
    if (lower == "music") {
        g_screen = SCREEN_MUSIC;
        draw_music_screen();
        Serial.println("[Music] local=" +
                       String(music_get_count() - music_get_cloud_count()) +
                       " cloud=" + String(music_get_cloud_count()));
        return;
    }
    if (lower == "cloud" || lower == "cloud list") {
        int n = music_get_cloud_count();
        int base = music_get_count() - n;
        for (int i = 0; i < n; i++) {
            Serial.println(String(base + i) + ". " + music_get_cloud_name(i) +
                           " " + music_get_cloud_url(i));
        }
        if (n == 0) Serial.println("No cloud tracks in /cloud.txt");
        return;
    }
    if (lower.startsWith("cloud play ")) {
        int n = input.substring(11).toInt();
        int base = music_get_count() - music_get_cloud_count();
        int idx = base + n;
        if (music_play(idx)) {
            Serial.println("[Music] Playing cloud " + music_get_name(idx));
        } else {
            Serial.println("[Music] Cannot play cloud index " + String(n));
        }
        return;
    }
    if (lower == "tetris") {
        g_screen = SCREEN_TETRIS;
        draw_tetris_screen();
        Serial.println("[Tetris] started");
        return;
    }
    if (lower.startsWith("play ")) {
        int idx = input.substring(5).toInt();
        if (music_play(idx)) {
            Serial.println("[Music] Playing " + music_get_name(idx));
        } else {
            Serial.println("[Music] Cannot play index " + String(idx));
        }
        return;
    }
    if (lower == "stop") {
        music_stop();
        Serial.println("[Music] stopped");
        return;
    }
    if (lower == "vol+" || lower == "vol up") {
        audio_set_volume(audio_get_volume() + 10);
        Serial.println("[Audio] Volume " + String(audio_get_volume()));
        return;
    }
    if (lower == "vol-" || lower == "vol down") {
        audio_set_volume(audio_get_volume() - 10);
        Serial.println("[Audio] Volume " + String(audio_get_volume()));
        return;
    }
    if (lower.startsWith("vol ")) {
        audio_set_volume(input.substring(4).toInt());
        Serial.println("[Audio] Volume " + String(audio_get_volume()));
        return;
    }
    if (lower == "vol") {
        Serial.println("[Audio] Volume " + String(audio_get_volume()) +
                       " (vol <0-100> | vol+ | vol-)");
        return;
    }
    if (lower == "ipconfig") {
        Serial.print(cmd_ipconfig());
        return;
    }
    if (lower.startsWith("ping ")) {
        Serial.print(cmd_ping(input.substring(5)));
        return;
    }
    if (lower.startsWith("tone ")) {
        int freq = input.substring(5).toInt();
        if (freq < 100 || freq > 5000) freq = 1000;
        if (!audio_beep(freq, 1200)) {
            Serial.println("[Audio] output not initialized");
        } else {
            Serial.println("[Audio] Tone " + String(freq) + "Hz played");
        }
        return;
    }
    if (lower == "beep") {
        if (!audio_beep(1000, 1200)) {
            Serial.println("[Audio] output not initialized");
        } else {
            Serial.println("[Audio] Beep played");
        }
        return;
    }
    if (lower == "ble") {
        g_screen = SCREEN_BLE;
        draw_ble_screen();
        return;
    }
    if (lower == "wifi history") {
        int n = wifi_history_count();
        for (int i = 0; i < n; i++) {
            Serial.println(String(i + 1) + ". " + wifi_history_ssid(i) +
                           " / " + mask_pass(wifi_history_pass(i)));
        }
        return;
    }
    if (lower.startsWith("ota ")) {
        String url = input.substring(4);
        url.trim();
        Serial.println(ota_start_from_url(url)
                       ? "[OTA] download started"
                       : "[OTA] start failed (use https:// URL)");
        return;
    }
    if (lower == "ota") {
        Serial.println("Usage: ota https://host/firmware.bin");
        return;
    }
    if (lower.startsWith("wifi set ")) {
        String rest = input.substring(9);
        int sp = rest.indexOf(' ');
        if (sp > 0) {
            wifi_save_credentials(rest.substring(0, sp), rest.substring(sp + 1));
            Serial.println("[OK] WiFi saved. Rebooting...");
            delay(300);
            ESP.restart();
        } else {
            Serial.println("Usage: wifi set <ssid> <password>");
        }
        return;
    }
    if (lower == "provider") {
        Serial.println("\n========== LLM Providers ==========");
        Serial.print(provider_list_text());
        Serial.println("===================================\n");
        return;
    }
    if (lower.startsWith("provider ")) {
        String arg = input.substring(9);
        arg.trim();
        int idx = arg.toInt();
        bool ok = (idx > 0 || arg == "0") ? llm_set_provider(idx) : llm_set_provider_by_name(arg);
        Serial.println(ok ? "[OK] " + llm_get_provider_name() : "[ERR] Provider not found");
        if (ok) draw_home_screen();
        return;
    }
    if (lower == "calib" || lower.startsWith("calib ")) {
        if (lower == "calib reset") {
            touch_calib_reset();
            Serial.println("[OK] Calibration reset");
            return;
        }
        if (lower == "calib show") {
            Serial.println("X: " + String(g_touchCalib.xMin) + "-" + String(g_touchCalib.xMax));
            Serial.println("Y: " + String(g_touchCalib.yMin) + "-" + String(g_touchCalib.yMax));
            Serial.println("swap=" + String(g_touchCalib.swapXY) +
                          " invX=" + String(g_touchCalib.invertX) +
                          " invY=" + String(g_touchCalib.invertY));
            return;
        }
        touch_calib_run_interactive();
        draw_home_screen();
        return;
    }
    if (lower.startsWith("say ")) {
        String text = input.substring(4);
        text.trim();
        if (text.length() > 0) {
            Serial.println("[TTS] speaking " + String(text.length()) + " chars...");
            bool ok = tts_speak(text);
            Serial.println(ok ? "[TTS] done" : "[TTS] failed");
        } else {
            Serial.println("Usage: say <text>");
        }
        return;
    }
    if (lower == "tts stop" || lower == "stop say" || lower == "stopsay") {
        tts_stop();
        Serial.println("[TTS] stop requested");
        return;
    }
    if (lower == "record") {
        g_screen = SCREEN_TERMINAL;
        g_termInput = "record";
        run_terminal_command();
        return;
    }
    if (lower == "voice" || lower == "voice chat") {
        g_screen = SCREEN_CHAT;
        draw_chat_screen();
        run_voice_chat();
        Serial.println("[Voice] chat done");
        return;
    }
    if (lower == "continuity" || lower == "meter") {
        g_screen = SCREEN_CONTINUITY;
        draw_continuity_screen();
        return;
    }
    if (lower == "tap" || lower.startsWith("tap ")) {
        int sp1 = input.indexOf(' ');
        if (sp1 > 0) {
            String rest = input.substring(sp1 + 1);
            int sp2 = rest.indexOf(' ');
            if (sp2 > 0) {
                int x = rest.substring(0, sp2).toInt();
                int y = rest.substring(sp2 + 1).toInt();
                handle_touch_xy(x, y);
                Serial.println("[Debug] tap(" + String(x) + "," + String(y) + ")");
                return;
            }
        }
        Serial.println("Usage: tap <x> <y>");
        return;
    }
    if (lower == "screen") {
        Serial.println("[Screen] current=" + String((int)g_screen));
        return;
    }
    // 普通文本进入聊天
    g_screen = SCREEN_CHAT;
    g_chatInput = input;
    draw_chat_screen();
    send_chat();
}

static void handle_serial() {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r') {
            if (Serial.peek() == '\n') continue;
            if (serialBuf.length() > 0) {
                process_serial_line(serialBuf);
                serialBuf = "";
            }
        } else if (c == '\n') {
            if (serialBuf.length() > 0) {
                process_serial_line(serialBuf);
                serialBuf = "";
            }
        } else {
            if (serialBuf.length() < 256) {
                serialBuf += c;
            }
        }
    }
}

void os_setup() {
    Serial.begin(115200);
    delay(2000);
    uint32_t bootStart = millis();

    display_init();
    show_boot_animation();
    audio_init();
    audio_boot_melody();

    if (VOLT_SENSE_PIN >= 0) {
        analogSetPinAttenuation(VOLT_SENSE_PIN, ADC_11db);
        Serial.println("[Monitor] ADC sense pin " + String(VOLT_SENSE_PIN) +
                       " raw=" + String(analogRead(VOLT_SENSE_PIN)));
    }

    pinMode(PROBE_A_PIN, OUTPUT);
    digitalWrite(PROBE_A_PIN, LOW);
    pinMode(PROBE_B_PIN, INPUT_PULLUP);
    g_contLastOk = digitalRead(PROBE_B_PIN) == LOW;

    if (psramFound()) {
        DBG_PRINTLN("[PSRAM] OK, size: " + String(ESP.getPsramSize() / 1024) + " KB");
    } else {
        DBG_PRINTLN("[PSRAM] not found");
    }

    touch_calib_load();
    touch_init();
    audio_mic_init();
    music_init();

    // BLE 先启动 (扫描需要), 避免与 WiFi 共存初始化顺序问题;
    // 配网服务本身只在 AP 模式下开启
    ble_stack_init();

    if (wifi_connect()) {
        DBG_PRINTLN("[WiFi] connected: " + wifi_get_ip_display());
    } else {
        wifi_start_ap();
        ble_provision_start();
        web_server_start();
    }
    configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");

    DBG_PRINTLN("[LLM] Provider: " + llm_get_provider_name());
    while (millis() - bootStart < 5000) {
        delay(20);
    }
    g_screen = SCREEN_HOME;
    draw_home_screen();
    // 关键外设初始化完成后确认固件可长期运行 (bootloader 回滚保护)
    ota_mark_valid();
    DBG_PRINTLN("[OS] ESP32 AI OS v1.0.0 ready");
    Serial.println("[OS] Ready. Commands: home | apps | chat | term | settings | wifi | weather | wifi map | music | cloud | tetris | calib | record | voice | continuity | say <text> | ota <https-url>");
}

void os_loop() {
    static uint32_t lastLogRefresh = 0;
    static uint32_t lastHomeRefresh = 0;
    static uint32_t lastWeatherAnim = 0;
    static uint32_t lastContPoll = 0;
    static bool lastTtsBusy = false;

    wifi_keep_alive();
    web_server_handle();
    ble_provision_loop();

    if (g_screen == SCREEN_LOGS && millis() - lastLogRefresh > 500) {
        lastLogRefresh = millis();
        String text = logger_get_recent();
        if (text != g_lastLogText) {
            g_lastLogText = text;
            update_logs_view();
        }
    }
    if (g_screen == SCREEN_WEATHER && g_lastWeatherCheck > 0 &&
        millis() - g_lastWeatherCheck > WEATHER_REFRESH_MS) {
        draw_weather_screen();
    }
    if (g_screen == SCREEN_TETRIS && tetris_is_active() && tetris_update()) {
        tetris_draw();
    }
    if (g_screen == SCREEN_WEATHER && weather_has_data() &&
        millis() - lastWeatherAnim > 200) {
        lastWeatherAnim = millis();
        g_weatherAnim++;
        draw_weather_icon_region(g_weatherAnim);
    }
    if (g_screen == SCREEN_CONTINUITY && millis() - lastContPoll > 150) {
        lastContPoll = millis();
        update_continuity_poll();
    }
    if (g_screen == SCREEN_HOME && millis() - lastHomeRefresh > 1000) {
        lastHomeRefresh = millis();
        update_home_clock();
    }
    if (lastTtsBusy && !tts_is_busy() && g_screen == SCREEN_CHAT) {
        draw_chat_screen();
    }
    lastTtsBusy = tts_is_busy();

    handle_serial();
    handle_touch();
    delay(5);
}
