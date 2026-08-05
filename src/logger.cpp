#include "logger.h"

static String logBuffer;
static const int LOG_MAX = 4096;

static void trim_log() {
    if ((int)logBuffer.length() > LOG_MAX) {
        logBuffer = logBuffer.substring(logBuffer.length() - LOG_MAX);
    }
}

void logger_print(const String& s) {
    Serial.print(s);
    logBuffer += s;
    trim_log();
}

void logger_println(const String& s) {
    Serial.println(s);
    logBuffer += s;
    logBuffer += "\n";
    trim_log();
}

String logger_get_recent() {
    return logBuffer;
}

void logger_clear() {
    logBuffer = "";
}
