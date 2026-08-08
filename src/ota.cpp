#include "ota.h"
#include "config.h"
#include "logger.h"
#include "wifi_utils.h"
#include "cert_bundle.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define OTA_CHUNK_BYTES 4096

static TaskHandle_t g_otaTask = NULL;
static volatile bool g_otaRunning = false;

void ota_mark_valid() {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        logger_println("[OTA] running image marked valid (rollback cancelled)");
    } else if (err == ESP_ERR_NOT_FOUND) {
        // 非 OTA 验证态, 属正常情况
    } else {
        logger_println("[OTA] mark valid failed: " + String(esp_err_to_name(err)));
    }
}

void ota_mark_invalid_and_reboot() {
    logger_println("[OTA] marking image invalid and rebooting...");
    esp_ota_mark_app_invalid_rollback_and_reboot();
}

static void ota_task(void* pv) {
    String url = *reinterpret_cast<String*>(pv);
    delete reinterpret_cast<String*>(pv);

    logger_println("[OTA] fetching: " + url);

    WiFiClientSecure client;
    client.setCACertBundle(x509_crt_bundle);
    client.setHandshakeTimeout(15000);

    HTTPClient http;
    http.setConnectTimeout(OTA_CONNECT_TIMEOUT_MS);
    http.setTimeout(OTA_STALL_TIMEOUT_MS / 1000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setRedirectLimit(4);
    http.setReuse(false);
    http.setUserAgent("ESP32-AI-OS-OTA/1.0");

    bool ok = http.begin(client, url);
    if (!ok) {
        logger_println("[OTA] HTTP begin failed");
        goto done;
    }

    if (http.GET() != 200) {
        logger_println("[OTA] HTTP GET failed");
        goto done_http;
    }

    {
        long size = http.getSize();
        if (size > (long)OTA_MAX_FW_BYTES) {
            logger_println("[OTA] firmware too large: " + String(size));
            goto done_http;
        }

        WiFiClient* stream = http.getStreamPtr();
        if (!stream) {
            logger_println("[OTA] no response stream");
            goto done_http;
        }

        size_t expected = (size > 0) ? (size_t)size : (size_t)UPDATE_SIZE_UNKNOWN;
        if (!Update.begin(expected)) {
            logger_println("[OTA] Update.begin failed");
            goto done_http;
        }

        uint8_t* buf = (uint8_t*)heap_caps_malloc(OTA_CHUNK_BYTES, MALLOC_CAP_SPIRAM);
        if (!buf) {
            Update.abort();
            logger_println("[OTA] OOM");
            goto done_http;
        }

        size_t total = 0;
        uint32_t lastDataAt = millis();
        uint32_t lastLogAt = millis();
        bool complete = false;

        while (true) {
            if (stream->available() > 0) {
                int want = min((int)OTA_CHUNK_BYTES, stream->available());
                int n = stream->read(buf, want);
                if (n > 0) {
                    if (Update.write(buf, n) != (size_t)n) {
                        logger_println("[OTA] flash write failed");
                        break;
                    }
                    total += n;
                    lastDataAt = millis();
                    if (millis() - lastLogAt > 2000) {
                        lastLogAt = millis();
                        logger_println("[OTA] progress " + String(total / 1024) + " KB");
                    }
                    continue;
                }
                if (n < 0) break;
            }

            if (size >= 0 && total >= (size_t)size) {
                complete = true;
                break;
            }
            if (!stream->connected() && stream->available() == 0) {
                complete = true;
                break;
            }
            if (millis() - lastDataAt > OTA_STALL_TIMEOUT_MS) {
                logger_println("[OTA] download stalled");
                break;
            }
            delay(2);
        }

        heap_caps_free(buf);
        if (!complete || total == 0 || (size >= 0 && total < (size_t)size)) {
            Update.abort();
            logger_println("[OTA] incomplete download (" + String(total) + " bytes)");
            goto done_http;
        }

        logger_println("[OTA] downloaded " + String(total) + " bytes, verifying...");
        if (!Update.end(true)) {
            Update.abort();
            logger_println("[OTA] Update.end failed: " + String(Update.errorString()));
            goto done_http;
        }

        logger_println("[OTA] success, rebooting...");
        http.end();
        delay(500);
        ESP.restart();
    }

done_http:
    http.end();
done:
    client.stop();
    g_otaRunning = false;
    g_otaTask = NULL;
    vTaskDelete(NULL);
}

bool ota_start_from_url(const String& url) {
    if (g_otaRunning) return false;
    if (!url.startsWith("https://")) {
        logger_println("[OTA] only https:// URLs are allowed");
        return false;
    }
    if (url.length() > 512) return false;

    String* pv = new String(url);
    if (!pv) return false;
    g_otaRunning = true;
    if (xTaskCreatePinnedToCore(ota_task, "ota", 16384, pv, 1, &g_otaTask, 1) != pdPASS) {
        delete pv;
        g_otaRunning = false;
        g_otaTask = NULL;
        return false;
    }
    return true;
}

bool ota_is_running() {
    return g_otaRunning;
}
