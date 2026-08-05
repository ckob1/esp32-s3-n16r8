/**
 * wifi_utils.cpp - WiFi 连接 + 自动重连 (含详细诊断)
 */
#include "wifi_utils.h"
#include <Preferences.h>

static uint32_t lastCheck = 0;
static String savedSsid;
static String savedPass;

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
    if (ssid.length() == 0) return false;

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
    if (wifi_is_ap_mode()) return "ESP32-AI-Setup";
    if (WiFi.status() == WL_CONNECTED) return WiFi.SSID();
    return savedSsid;
}

void wifi_start_ap() {
    DBG_PRINTLN("[WiFi] Starting AP mode...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32-AI-Setup", "12345678");
    DBG_PRINTLN("[WiFi] AP: ESP32-AI-Setup / 12345678 @ 192.168.4.1");
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
        case WL_NO_SHIELD:        return "NO_SHIELD (硬件未就绪)";
        case WL_IDLE_STATUS:      return "IDLE";
        case WL_NO_SSID_AVAIL:    return "NO_SSID_AVAIL (找不到 SSID, 检查名称/2.4G)";
        case WL_SCAN_COMPLETED:   return "SCAN_COMPLETED";
        case WL_CONNECTED:        return "CONNECTED";
        case WL_CONNECT_FAILED:   return "CONNECT_FAILED (密码错误或拒绝)";
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
    DBG_PRINTLN("[WiFi] === 开始连接 WiFi ===");
    String targetSsid = WIFI_SSID;
    String targetPass = WIFI_PWD;
    if (wifi_load_credentials()) {
        targetSsid = savedSsid;
        targetPass = savedPass;
    } else if (targetSsid.length() == 0) {
        DBG_PRINTLN("[WiFi] No saved credentials. Use AP setup.");
        return false;
    }
    DBG_PRINTLN("[WiFi] SSID: " + targetSsid);
    DBG_PRINTLN("[WiFi] PWD:  " + targetPass);
    DBG_PRINTLN("[WiFi] MAC:  " + WiFi.macAddress());

    // 1. 检查是否处于 STA 模式
    WiFi.mode(WIFI_STA);
    // 注意: 不要在这里 WiFi.setSleep(false), 会与后续 BLE 共存初始化冲突导致 abort
    // ESP32-S3 默认低功率, 调高发射功率改善连接
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    // 2. 扫描看看目标 SSID 在不在
    DBG_PRINTLN("[WiFi] 扫描附近 WiFi (耗时约 3-5 秒)...");
    int n = WiFi.scanNetworks();
    DBG_PRINTLN("[WiFi] 扫描到 " + String(n) + " 个网络:");
    bool found = false;
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
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
        DBG_PRINTLN("[WiFi] ❌ 没扫到目标 SSID '" + targetSsid + "'");
        DBG_PRINTLN("[WiFi] 检查清单:");
        DBG_PRINTLN("  1. SSID 拼写对吗? (大小写敏感)");
        DBG_PRINTLN("  2. WiFi 是 2.4G 频段吗? (ESP32-S3 不支持 5G)");
        DBG_PRINTLN("  3. 路由器是否隐藏了 SSID?");
        DBG_PRINTLN("  4. 路由器是否开启了 MAC 过滤?");
    }

    // 3. 开始连接
    DBG_PRINTLN("[WiFi] 调用 WiFi.begin()...");
    WiFi.begin(targetSsid.c_str(), targetPass.c_str());

    uint32_t t0 = millis();
    int lastStatus = -1;
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < WIFI_TIMEOUT_MS) {
        delay(300);
        wl_status_t s = WiFi.status();
        if ((int)s != lastStatus) {
            DBG_PRINTLN("[WiFi] 状态变更 → " + String(wifi_status_str(s)) +
                        " (" + String((millis()-t0)/1000) + "s)");
            lastStatus = (int)s;
        }
    }

    wl_status_t finalStatus = WiFi.status();
    DBG_PRINTLN("[WiFi] 最终状态: " + String(wifi_status_str(finalStatus)));

    if (finalStatus == WL_CONNECTED) {
        DBG_PRINTLN("[WiFi] ✅ 连接成功");
        DBG_PRINTLN("[WiFi] IP:       " + WiFi.localIP().toString());
        DBG_PRINTLN("[WiFi] 网关:     " + WiFi.gatewayIP().toString());
        DBG_PRINTLN("[WiFi] 子网掩码: " + WiFi.subnetMask().toString());
        DBG_PRINTLN("[WiFi] DNS:      " + WiFi.dnsIP().toString());
        DBG_PRINTLN("[WiFi] RSSI:     " + String(WiFi.RSSI()) + " dBm");
        return true;
    } else {
        DBG_PRINTLN("[WiFi] ❌ 连接失败");
        DBG_PRINTLN("[WiFi] 排查清单:");
        DBG_PRINTLN("  1. 密码是否正确? (特殊字符/空格)");
        DBG_PRINTLN("  2. 路由器是否用了 WPA3-only? (ESP32-S3 老固件只支持 WPA2)");
        DBG_PRINTLN("  3. 路由器是否开启了 802.11ax (WiFi 6) only? 改成 b/g/n mixed");
        DBG_PRINTLN("  4. 手机热点建议用 2.4G + WPA2-Personal");
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
            DBG_PRINTLN("[WiFi] 断线, 尝试重连...");
            WiFi.reconnect();
            if (reconnectFails >= 4) {
                reconnectFails = 0;
                DBG_PRINTLN("[WiFi] 多次重连失败, 切换 AP 配网模式");
                wifi_start_ap();
            }
        }
    } else {
        reconnectFails = 0;
    }
}
