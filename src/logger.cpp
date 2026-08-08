#include "logger.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static String logBuffer;
static const int LOG_MAX = 4096;
static SemaphoreHandle_t logLock = NULL;

static void trim_log() {
    if ((int)logBuffer.length() > LOG_MAX) {
        logBuffer = logBuffer.substring(logBuffer.length() - LOG_MAX);
    }
}

void logger_print(const String& s) {
    if (!logLock) logLock = xSemaphoreCreateMutex();
    if (logLock) xSemaphoreTake(logLock, portMAX_DELAY);
    Serial.print(s);
    logBuffer += s;
    trim_log();
    if (logLock) xSemaphoreGive(logLock);
}

void logger_println(const String& s) {
    if (!logLock) logLock = xSemaphoreCreateMutex();
    if (logLock) xSemaphoreTake(logLock, portMAX_DELAY);
    Serial.println(s);
    logBuffer += s;
    logBuffer += "\n";
    trim_log();
    if (logLock) xSemaphoreGive(logLock);
}

String logger_get_recent() {
    if (!logLock) logLock = xSemaphoreCreateMutex();
    if (logLock) xSemaphoreTake(logLock, portMAX_DELAY);
    String s = logBuffer;
    if (logLock) xSemaphoreGive(logLock);
    return s;
}

void logger_clear() {
    if (!logLock) logLock = xSemaphoreCreateMutex();
    if (logLock) xSemaphoreTake(logLock, portMAX_DELAY);
    logBuffer = "";
    if (logLock) xSemaphoreGive(logLock);
}
