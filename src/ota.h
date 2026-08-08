#pragma once
#include <Arduino.h>

// 开机初始化完成后调用: 确认当前固件可长期运行, 防止 bootloader 回滚
void ota_mark_valid();

// 当前固件异常时显式回滚到上一版本并重启
void ota_mark_invalid_and_reboot();

// 从 HTTPS URL 流式下载新固件并写入下一 OTA 槽 (后台任务, 不阻塞 UI)
bool ota_start_from_url(const String& url);
bool ota_is_running();
