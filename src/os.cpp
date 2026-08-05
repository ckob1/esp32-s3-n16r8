#include "os.h"
#include "config.h"
#include "display.h"
#include "cn_font.h"
#include "touch.h"
#include "touch_calib.h"
#include "audio.h"
#include "wifi_utils.h"
#include "llm.h"
#include "web_server.h"
#include "logger.h"
#include "ble_provision.h"
#include "apple_logo.h"
#include <WiFi.h>
#include <esp_heap_caps.h>

enum AppScreen {
    SCREEN_HOME,
    SCREEN_CHAT,
    SCREEN_TERMINAL,
    SCREEN_SETTINGS,
    SCREEN_WIFI,
    SCREEN_LOGS,
    SCREEN_INFO
};

static AppScreen g_screen = SCREEN_HOME;
static bool g_llmBusy = false;

static String g_chatLog;
static String g_chatInput;
static String g_termLog;
static String g_termInput;

static Button g_back;
static Button g_homeButtons[6];
static Button g_settingsButtons[5];
static Button g_wifiButtons[3];

static Button g_keys[40];
static int g_keyCount = 0;

static String serialBuf;

static void draw_home_screen();
static void draw_chat_screen();
static void draw_terminal_screen();
static void draw_settings_screen();
static void draw_wifi_screen();
static void draw_logs_screen();
static void draw_info_screen();

static void trim_log(String& s, int maxLen) {
    if ((int)s.length() > maxLen) {
        s = s.substring(s.length() - maxLen);
    }
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

static void draw_boot_frame(int frame) {
    const int frames = 16;
    float angle = 2.0f * PI * frame / frames;
    float cosA = cosf(angle);
    float sinA = sinf(angle);

    uint16_t* buf = (uint16_t*)heap_caps_malloc(
        APPLE_LOGO_W * APPLE_LOGO_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!buf) return;

    int cx = APPLE_LOGO_W / 2;
    int cy = APPLE_LOGO_H / 2;

    for (int y = 0; y < APPLE_LOGO_H; y++) {
        for (int x = 0; x < APPLE_LOGO_W; x++) {
            int sx = (int)(cosA * (x - cx) - sinA * (y - cy) + cx);
            int sy = (int)(sinA * (x - cx) + cosA * (y - cy) + cy);
            if (sx >= 0 && sx < APPLE_LOGO_W && sy >= 0 && sy < APPLE_LOGO_H) {
                buf[y * APPLE_LOGO_W + x] = pgm_read_word(&APPLE_LOGO_DATA[sy * APPLE_LOGO_W + sx]);
            } else {
                buf[y * APPLE_LOGO_W + x] = 0x0000;
            }
        }
    }

    tft.fillScreen(ILI9341_BLACK);
    tft.drawRGBBitmap(
        (tft.width() - APPLE_LOGO_W) / 2,
        (tft.height() - APPLE_LOGO_H) / 2 - 14,
        buf, APPLE_LOGO_W, APPLE_LOGO_H);
    display_draw_text(78, 184, "ESP32 AI OS v1.0.0", ILI9341_GREEN, 1);
    display_draw_text(78, 196, "Initializing...", ILI9341_CYAN, 1);

    heap_caps_free(buf);
}

static void show_boot_animation() {
    for (int i = 0; i < 16; i++) {
        draw_boot_frame(i);
        delay(70);
    }
    tft.fillScreen(ILI9341_BLACK);
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
    const int keyH = 22;
    const int gap = 2;
    const int x0 = 2;

    tft.fillRect(0, 134, tft.width(), tft.height() - 134, ILI9341_BLACK);
    g_keyCount = 0;

    const char* row1 = "1234567890";
    const char* row2 = "qwertyuiop";
    const char* row3 = "asdfghjkl";
    const char* row4 = "zxcvbnm";

    for (int i = 0; i < 10; i++) {
        add_key(x0 + i * (keyW + gap), 136, keyW, keyH, String(row1[i]));
    }
    for (int i = 0; i < 10; i++) {
        add_key(x0 + i * (keyW + gap), 160, keyW, keyH, String(row2[i]));
    }
    for (int i = 0; i < 9; i++) {
        add_key(x0 + i * (keyW + gap), 184, keyW, keyH, String(row3[i]));
    }
    add_key(x0 + 9 * (keyW + gap), 184, keyW, keyH, "<-");

    for (int i = 0; i < 7; i++) {
        add_key(x0 + i * (keyW + gap), 208, keyW, keyH, String(row4[i]));
    }
    add_key(221, 208, 46, keyH, "Space");
    add_key(269, 208, 49, keyH, "Send");
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

static void draw_home_screen() {
    tft.fillScreen(ILI9341_BLACK);
    tft.fillRect(0, 0, tft.width(), 24, ILI9341_BLACK);
    tft.drawFastHLine(0, 23, tft.width(), ILI9341_GREEN);
    display_draw_text(8, 5, "ESP32 AI OS", ILI9341_GREEN, 2);
    tft.drawFastHLine(0, tft.height() - 18, tft.width(), ILI9341_GREEN);

    String ip = wifi_is_ap_mode() ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    String info = "WiFi: " + wifi_get_ssid_display() + " (" + ip + ")";
    display_draw_text(8, 28, info, ILI9341_GREEN, 1);
    display_draw_text(8, 40, "Provider: " + llm_get_provider_name(), ILI9341_GREEN, 1);

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
    g_homeButtons[4] = draw_app_button(x0 + (w + gap), y2, w, h, "Logs");
    g_homeButtons[5] = draw_app_button(x0 + 2 * (w + gap), y2, w, h, "Info");

    display_draw_text(8, 140, "HTTP: http://" + ip, ILI9341_CYAN, 1);
    display_draw_text(8, 154, "BLE: ESP32-AI-Setup", ILI9341_CYAN, 1);
    display_draw_text(8, 168, "Type any text in serial to chat", ILI9341_GREEN, 1);
    display_draw_text(8, 224, "Ready", ILI9341_GREEN, 1);
}

static void draw_chat_screen() {
    draw_header("Chat");

    tft.fillRect(0, 26, tft.width(), 92, ILI9341_BLACK);
    draw_bounded_ascii(8, 30, 312, 86, g_chatLog, UI_COLOR_TEXT);

    tft.fillRect(0, 118, tft.width(), 14, ILI9341_BLACK);
    display_draw_text(8, 120, "> " + g_chatInput, UI_COLOR_TEXT, 1);

    draw_keyboard();
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
    const int h = 24;
    const int x = 10;
    const int y0 = 56;
    const int gap = 5;

    g_settingsButtons[0] = draw_app_button(x, y0, w, h, "Calibrate 4-Point");
    g_settingsButtons[1] = draw_app_button(x, y0 + (h + gap), w, h, "Reset Calibration");
    g_settingsButtons[2] = draw_app_button(x, y0 + 2 * (h + gap), w, h, "Start WiFi AP");
    g_settingsButtons[3] = draw_app_button(x, y0 + 3 * (h + gap), w, h, "Next Provider");
    g_settingsButtons[4] = draw_app_button(x, y0 + 4 * (h + gap), w, h, "System Info");
}

static void draw_wifi_screen() {
    draw_header("WiFi Setup");

    String mode = wifi_is_ap_mode() ? "AP Mode" : "STA Mode";
    display_draw_text(8, 30, "Mode: " + mode, UI_COLOR_OK, 1);
    display_draw_text(8, 44, "IP: " + wifi_get_ip_display(), UI_COLOR_TEXT, 1);

    if (wifi_is_ap_mode()) {
        display_draw_text(8, 58, "SSID: ESP32-AI-Setup", UI_COLOR_WARN, 1);
        display_draw_text(8, 72, "Pass: 12345678", UI_COLOR_WARN, 1);
        display_draw_text(8, 86, "Open http://192.168.4.1", UI_COLOR_WARN, 1);
    } else {
        display_draw_text(8, 58, "Saved SSID: " + wifi_get_saved_ssid(), UI_COLOR_TEXT, 1);
        display_draw_text(8, 72, "Open http://" + wifi_get_ip_display(), UI_COLOR_WARN, 1);
    }
    display_draw_text(8, 86, "BLE: ESP32-AI-Setup", UI_COLOR_WARN, 1);
    display_draw_text(8, 100, "Send: SSID:PASS", UI_COLOR_WARN, 1);

    g_wifiButtons[0] = draw_app_button(10, 120, 300, 28, "Start AP");
    g_wifiButtons[1] = draw_app_button(10, 156, 300, 28, "Clear Credentials");
    g_wifiButtons[2] = draw_app_button(10, 192, 300, 28, "Reboot");
}

static void draw_logs_screen() {
    draw_header("Logs");
    tft.fillRect(0, 26, tft.width(), 206, ILI9341_BLACK);
    draw_bounded_ascii(8, 30, 312, 200, logger_get_recent(), ILI9341_GREEN);
}

static void draw_info_screen() {
    draw_header("System Info");

    String s;
    s += "Firmware: v1.0.0\n";
    s += "PSRAM: " + String(ESP.getPsramSize() / 1024) + " KB\n";
    s += "Heap: " + String(ESP.getFreeHeap()) + " B\n";
    s += "Provider: " + llm_get_provider_name() + "\n";
    s += "WiFi: " + wifi_get_ip_display() + "\n";
    s += "Model: agnes-2.5-flash\n";

    draw_bounded_ascii(8, 30, 312, 180, s, UI_COLOR_TEXT);
}

static void append_terminal(const String& text) {
    g_termLog += text;
    trim_log(g_termLog, 1800);
}

static void run_terminal_command() {
    String cmd = g_termInput;
    g_termInput = "";
    append_terminal("> " + cmd + "\n");

    String lower = cmd;
    lower.toLowerCase();
    String out;

    if (lower == "help") {
        out = "help | clear | provider | calib | calib show | wifi | record | reboot\n";
    } else if (lower == "clear") {
        g_termLog = "";
        draw_terminal_screen();
        return;
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

    g_chatInput = "";
    g_chatLog += "You: " + prompt + "\n";
    trim_log(g_chatLog, 1800);
    draw_chat_screen();

    g_llmBusy = true;
    tft.fillRect(0, 118, tft.width(), 14, ILI9341_BLACK);
    display_draw_text(8, 120, "Thinking...", UI_COLOR_WARN, 1);

    LLMResult r = llm_chat(prompt);
    if (r.ok) {
        g_chatLog += "AI: " + r.content + "\n";
        audio_ok_sound();
    } else {
        g_chatLog += "Error: " + r.errorMsg + "\n";
        audio_error_sound();
    }
    trim_log(g_chatLog, 1800);
    g_llmBusy = false;
    draw_chat_screen();
}

static void handle_keyboard_touch(Button* hit) {
    if (!hit) return;

    String* input = (g_screen == SCREEN_CHAT) ? &g_chatInput : &g_termInput;
    if (hit->label == "<-") {
        if (input->length() > 0) {
            input->remove(input->length() - 1);
        }
    } else if (hit->label == "Space") {
        *input += ' ';
    } else if (hit->label == "Send") {
        if (g_screen == SCREEN_CHAT) {
            send_chat();
        } else {
            run_terminal_command();
        }
        return;
    } else {
        *input += hit->label;
    }

    if (g_screen == SCREEN_CHAT) {
        draw_chat_screen();
    } else {
        draw_terminal_screen();
    }
}

static void handle_touch() {
    TouchPoint tp = touch_read();
    if (!tp.pressed) return;

    audio_touch_sound();

    if (g_screen != SCREEN_HOME && display_point_in_button(g_back, tp.x, tp.y)) {
        g_screen = SCREEN_HOME;
        draw_home_screen();
        touch_wait_release();
        return;
    }

    if (g_screen == SCREEN_HOME) {
        for (int i = 0; i < 6; i++) {
            if (display_point_in_button(g_homeButtons[i], tp.x, tp.y)) {
                touch_wait_release();
                if (i == 0) { g_screen = SCREEN_CHAT; draw_chat_screen(); }
                else if (i == 1) { g_screen = SCREEN_TERMINAL; draw_terminal_screen(); }
                else if (i == 2) { g_screen = SCREEN_SETTINGS; draw_settings_screen(); }
                else if (i == 3) { g_screen = SCREEN_WIFI; draw_wifi_screen(); }
                else if (i == 4) { g_screen = SCREEN_LOGS; draw_logs_screen(); }
                else { g_screen = SCREEN_INFO; draw_info_screen(); }
                return;
            }
        }
    } else if (g_screen == SCREEN_CHAT || g_screen == SCREEN_TERMINAL) {
        for (int i = 0; i < g_keyCount; i++) {
            if (display_point_in_button(g_keys[i], tp.x, tp.y)) {
                touch_wait_release();
                handle_keyboard_touch(&g_keys[i]);
                return;
            }
        }
    } else if (g_screen == SCREEN_SETTINGS) {
        for (int i = 0; i < 5; i++) {
            if (display_point_in_button(g_settingsButtons[i], tp.x, tp.y)) {
                touch_wait_release();
                if (i == 0) {
                    touch_calib_run_interactive();
                    draw_settings_screen();
                } else if (i == 1) {
                    touch_calib_reset();
                    draw_settings_screen();
                } else if (i == 2) {
                    wifi_start_ap();
                    web_server_start();
                    draw_wifi_screen();
                } else if (i == 3) {
                    llm_next_provider();
                    draw_settings_screen();
                } else {
                    g_screen = SCREEN_INFO;
                    draw_info_screen();
                }
                return;
            }
        }
    } else if (g_screen == SCREEN_WIFI) {
        for (int i = 0; i < 3; i++) {
            if (display_point_in_button(g_wifiButtons[i], tp.x, tp.y)) {
                touch_wait_release();
                if (i == 0) {
                    wifi_start_ap();
                    web_server_start();
                    draw_wifi_screen();
                } else if (i == 1) {
                    wifi_clear_credentials();
                    draw_wifi_screen();
                } else {
                    ESP.restart();
                }
                return;
            }
        }
    }

    touch_wait_release();
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
    if (lower == "wifi") {
        g_screen = SCREEN_WIFI;
        draw_wifi_screen();
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
    if (lower == "record") {
        g_screen = SCREEN_TERMINAL;
        g_termInput = "record";
        run_terminal_command();
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

    display_init();
    show_boot_animation();
    audio_init();
    audio_boot_melody();

    if (psramFound()) {
        DBG_PRINTLN("[PSRAM] OK, size: " + String(ESP.getPsramSize() / 1024) + " KB");
    } else {
        DBG_PRINTLN("[PSRAM] not found");
    }

    touch_calib_load();
    touch_init();
    audio_mic_init();

    // BLE 先启动, 避免与 WiFi 共存初始化顺序问题
    ble_provision_start();

    if (wifi_connect()) {
        DBG_PRINTLN("[WiFi] connected: " + wifi_get_ip_display());
    } else {
        wifi_start_ap();
    }
    web_server_start();

    DBG_PRINTLN("[LLM] Provider: " + llm_get_provider_name());
    g_screen = SCREEN_HOME;
    draw_home_screen();
    DBG_PRINTLN("[OS] ESP32 AI OS v1.0.0 ready");
    Serial.println("[OS] Ready. Commands: home | chat | term | settings | wifi | calib | record");
}

void os_loop() {
    static uint32_t lastLogRefresh = 0;

    wifi_keep_alive();
    web_server_handle();
    ble_provision_loop();

    if (g_screen == SCREEN_LOGS && millis() - lastLogRefresh > 500) {
        lastLogRefresh = millis();
        draw_logs_screen();
    }

    handle_serial();
    handle_touch();
    delay(5);
}
