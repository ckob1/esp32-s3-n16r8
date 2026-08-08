/**
 * wifi_utils.cpp - WiFi 连接 + 自动重连 (含详细诊断)
 */
#include "wifi_utils.h"
#include <Preferences.h>

static uint32_t lastCheck = 0;
static String savedSsid;
static String savedPass;

static const char* HISTORY_NS = "wifi_hist";

void wifi_add_history(const String& ssid, const String& pass) {
    if (ssid.length() == 0) return;

    Preferences prefs;
    if (!prefs.begin(HISTORY_NS, false)) {
        prefs.end();
        return;
    }

    int n = prefs.getInt("count", 0);
    for (int i = 0; i < n; i++) {
        String key = "ssid_" + String(i);
        if (prefs.getString(key.c_str(), "") == ssid) {
            String passKey = "pass_" + String(i);
            prefs.putString(passKey.c_str(), pass);
            prefs.end();
            return;
        }
    }

    if (n >= 8) {
        for (int i = 1; i < 8; i++) {
            String fromSsid = "ssid_" + String(i);
            String fromPass = "pass_" + String(i);
            String toSsid = "ssid_" + String(i - 1);
            String toPass = "pass_" + String(i - 1);
            prefs.putString(toSsid.c_str(), prefs.getString(fromSsid.c_str(), ""));
            prefs.putString(toPass.c_str(), prefs.getString(fromPass.c_str(), ""));
        }
        n = 7;
    }

    prefs.putString(("ssid_" + String(n)).c_str(), ssid);
    prefs.putString(("pass_" + String(n)).c_str(), pass);
    prefs.putInt("count", n + 1);
    prefs.end();
}

int wifi_history_count() {
    Preferences prefs;
    if (!prefs.begin(HISTORY_NS, false)) {
        prefs.end();
        return 0;
    }
    int n = prefs.getInt("count", 0);
    prefs.end();
    return n;
}

String wifi_history_ssid(int idx) {
    Preferences prefs;
    if (!prefs.begin(HISTORY_NS, false)) {
        prefs.end();
        return "";
    }
    String key = "ssid_" + String(idx);
    String v = prefs.getString(key.c_str(), "");
    prefs.end();
    return v;
}

String wifi_history_pass(int idx) {
    Preferences prefs;
    if (!prefs.begin(HISTORY_NS, false)) {
        prefs.end();
        return "";
    }
    String key = "pass_" + String(idx);
    String v = prefs.getString(key.c_str(), "");
    prefs.end();
    return v;
}

bool wifi_load_credentials() {
    Preferences prefs;
    if (!prefs.begin("wifi_cfg", true)) {
        prefs.end();
        savedSsid = "";
        savedPass = "";
        return false;
    }

    savedSsid = prefs.getString("ssid", "");
    savedPass = prefs.getString("pass", "");
    prefs.end();
    return savedSsid.length() > 0;
}

bool wifi_save_credentials(const String& ssid, const String& pass) {
    if (ssid.length() == 0 || ssid.length() > 64 || pass.length() > 128) return false;

    Preferences prefs;
    if (!prefs.begin("wifi_cfg", false)) {
        prefs.end();
        return false;
    }

    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();

    savedSsid = ssid;
    savedPass = pass;
    wifi_add_history(ssid, pass);
    return true;
}

void wifi_clear_credentials() {
    Preferences prefs;
    if (prefs.begin("wifi_cfg", false)) {
        prefs.clear();
        prefs.end();
    }
    savedSsid = "";
    savedPass = "";
}

bool wifi_has_saved_credentials() {
    return savedSsid.length() > 0;
}

String wifi_get_saved_ssid() {
    return savedSsid;
}

String wifi_get_ssid_display() {
    if (wifi_is_ap_mode()) return WIFI_AP_SSID;
    if (WiFi.status() == WL_CONNECTED) return WiFi.SSID();
    return savedSsid;
}

void wifi_start_ap() {
    DBG_PRINTLN("[WiFi] Starting AP mode...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    DBG_PRINTLN("[WiFi] AP: " + String(WIFI_AP_SSID) + " @ 192.168.4.1");
}

bool wifi_is_ap_mode() {
    wifi_mode_t mode = WiFi.getMode();
    return mode == WIFI_AP || mode == WIFI_AP_STA;
}

String wifi_get_ip_display() {
    if (wifi_is_ap_mode()) {
        return WiFi.softAPIP().toString();
    }
    return WiFi.localIP().toString();
}

// 把 WiFi.status() 转成文字, 方便调试
static const char* wifi_status_str(wl_status_t s) {
    switch (s) {
        case WL_NO_SHIELD:        return "NO_SHIELD";
        case WL_IDLE_STATUS:      return "IDLE";
        case WL_NO_SSID_AVAIL:    return "NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED:   return "SCAN_COMPLETED";
        case WL_CONNECTED:        return "CONNECTED";
        case WL_CONNECT_FAILED:   return "CONNECT_FAILED";
        case WL_CONNECTION_LOST:  return "CONNECTION_LOST";
        case WL_DISCONNECTED:     return "DISCONNECTED";
        default:                  return "UNKNOWN";
    }
}

static const char* wifi_auth_str(wifi_auth_mode_t a) {
    switch (a) {
        case WIFI_AUTH_OPEN:         return "OPEN";
        case WIFI_AUTH_WEP:          return "WEP";
        case WIFI_AUTH_WPA_PSK:      return "WPA";
        case WIFI_AUTH_WPA2_PSK:     return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-EAP";
        case WIFI_AUTH_WPA3_PSK:     return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        case WIFI_AUTH_WAPI_PSK:     return "WAPI";
        default:                     return "?";
    }
}

bool wifi_connect() {
    DBG_PRINTLN("[WiFi] === connect ===");
    String targetSsid = WIFI_SSID;
    String targetPass = WIFI_PWD;
    if (wifi_load_credentials()) {
        targetSsid = savedSsid;
        targetPass = savedPass;
    } else if (targetSsid.length() == 0) {
        DBG_PRINTLN("[WiFi] No saved credentials. Use AP setup.");
        return false;
    }
    targetSsid.trim();
    targetPass.trim();
    DBG_PRINTLN("[WiFi] SSID: " + targetSsid);
    // 安全: 绝不在串口日志中输出 WiFi 密码
    DBG_PRINTLN("[WiFi] MAC:  " + WiFi.macAddress());

    // 1. 检查是否处于 STA 模式
    WiFi.mode(WIFI_STA);
    // 注意: 不要在这里 WiFi.setSleep(false), 会与后续 BLE 共存初始化冲突导致 abort
    // ESP32-S3 默认低功率, 调高发射功率改善连接
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    // 2. 扫描看看目标 SSID 在不在
    DBG_PRINTLN("[WiFi] scanning...");
    int n = WiFi.scanNetworks();
    DBG_PRINTLN("[WiFi] found " + String(n) + " networks:");
    bool found = false;
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        ssid.trim();
        int rssi = WiFi.RSSI(i);
        int channel = WiFi.channel(i);
        wifi_auth_mode_t auth = WiFi.encryptionType(i);
        DBG_PRINTLN("  " + ssid + " (" + String(rssi) + " dBm, ch" + String(channel) +
                    ", " + String(wifi_auth_str(auth)) + ")");
        if (ssid == targetSsid) {
            found = true;
            DBG_PRINTLN("[WiFi] target auth=" + String(wifi_auth_str(auth)) +
                        " ch=" + String(channel));
        }
    }
    if (!found) {
        DBG_PRINTLN("[WiFi] target SSID not in this scan, still trying direct connect: '" + targetSsid + "'");
        DBG_PRINTLN("[WiFi] check:");
        DBG_PRINTLN("  1. SSID case sensitive?");
        DBG_PRINTLN("  2. 2.4G band? (S3 no 5G)");
        DBG_PRINTLN("  3. Hidden SSID?");
        DBG_PRINTLN("  4. MAC filter?");
    }

    // 3. 开始连接
    DBG_PRINTLN("[WiFi] WiFi.begin()...");
    WiFi.begin(targetSsid.c_str(), targetPass.c_str());

    uint32_t t0 = millis();
    int lastStatus = -1;
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < WIFI_TIMEOUT_MS) {
        delay(300);
        wl_status_t s = WiFi.status();
        if ((int)s != lastStatus) {
            DBG_PRINTLN("[WiFi] state -> " + String(wifi_status_str(s)) +
                        " (" + String((millis()-t0)/1000) + "s)");
            lastStatus = (int)s;
        }
    }

    wl_status_t finalStatus = WiFi.status();
    DBG_PRINTLN("[WiFi] final state: " + String(wifi_status_str(finalStatus)));

    if (finalStatus == WL_CONNECTED) {
        DBG_PRINTLN("[WiFi] connected");
        DBG_PRINTLN("[WiFi] IP:       " + WiFi.localIP().toString());
        DBG_PRINTLN("[WiFi] gateway:  " + WiFi.gatewayIP().toString());
        DBG_PRINTLN("[WiFi] mask:     " + WiFi.subnetMask().toString());
        DBG_PRINTLN("[WiFi] DNS:      " + WiFi.dnsIP().toString());
        DBG_PRINTLN("[WiFi] RSSI:     " + String(WiFi.RSSI()) + " dBm");
        return true;
    } else {
        DBG_PRINTLN("[WiFi] connect failed");
        DBG_PRINTLN("[WiFi] check:");
        DBG_PRINTLN("  1. Password correct?");
        DBG_PRINTLN("  2. WPA3-only? use WPA2");
        DBG_PRINTLN("  3. 802.11ax only? use b/g/n mixed");
        DBG_PRINTLN("  4. Use 2.4G + WPA2 hotspot");
        return false;
    }
}

void wifi_disconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

bool wifi_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}

void wifi_keep_alive() {
    static int reconnectFails = 0;

    if (millis() - lastCheck < 5000) return;
    lastCheck = millis();

    if (wifi_is_ap_mode()) return;

    if (!wifi_is_connected()) {
        if (!wifi_has_saved_credentials()) {
            wifi_start_ap();
        } else {
            reconnectFails++;
            DBG_PRINTLN("[WiFi] lost, reconnecting...");
            WiFi.reconnect();
            if (reconnectFails >= 4) {
                reconnectFails = 0;
                DBG_PRINTLN("[WiFi] reconnect failed, switching to AP");
                wifi_start_ap();
            }
        }
    } else {
        reconnectFails = 0;
    }
}
