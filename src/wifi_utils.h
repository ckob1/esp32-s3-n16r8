/**
 * wifi_utils.h - WiFi 连接管理
 */
#pragma once
#include "config.h"
#include <WiFi.h>

bool wifi_connect();
void wifi_disconnect();
bool wifi_is_connected();
void wifi_keep_alive();   // 在 loop 里调用, 处理断线重连

// WiFi 配网信息 (NVS 持久化)
bool wifi_load_credentials();
bool wifi_save_credentials(const String& ssid, const String& pass);
void wifi_clear_credentials();
bool wifi_has_saved_credentials();
String wifi_get_saved_ssid();
String wifi_get_ssid_display();

// AP 配置模式
void wifi_start_ap();
bool wifi_is_ap_mode();
String wifi_get_ip_display();
