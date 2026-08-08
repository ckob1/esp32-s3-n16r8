#include "weather.h"
#include "wifi_utils.h"
#include "cert_bundle.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static WeatherData g_weather;
static bool g_hasData = false;
static uint32_t g_fetchedAt = 0;
static bool g_fetching = false;

String weather_code_text(int code) {
    switch (code) {
        case 0:  return "Clear";
        case 1:  return "Mostly Clear";
        case 2:  return "Partly Cloudy";
        case 3:  return "Overcast";
        case 45:
        case 48: return "Fog";
        case 51:
        case 53:
        case 55: return "Drizzle";
        case 56:
        case 57:
        case 61:
        case 63:
        case 65:
        case 66:
        case 67:
        case 80:
        case 81:
        case 82: return "Rain";
        case 71:
        case 73:
        case 75:
        case 77:
        case 85:
        case 86: return "Snow";
        case 95:
        case 96:
        case 99: return "Thunderstorm";
        default: return "Unknown";
    }
}

String weather_wind_dir(float deg) {
    const char* dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int idx = (int)((deg + 22.5f) / 45.0f) % 8;
    return String(dirs[idx]);
}

WeatherData weather_get() {
    return g_weather;
}

bool weather_has_data() {
    return g_hasData;
}

bool weather_fetch() {
    if (g_fetching) return g_hasData;
    g_fetching = true;

    WeatherData d;
    d.city = WEATHER_CITY;
    d.temp = -999;
    d.feels = -999;
    d.humidity = -1;
    d.precip = -1;
    d.wind = -1;
    d.windDir = -1;
    d.pressure = -1;
    d.code = -1;

    if (!wifi_is_connected()) {
        d.error = "WiFi not connected";
        g_weather = d;
        g_fetching = false;
        return false;
    }

    String tz = WEATHER_TZ;
    tz.replace("/", "%2F");
    String url = "https://api.open-meteo.com/v1/forecast?";
    url += "latitude=" + String(WEATHER_LAT, 4);
    url += "&longitude=" + String(WEATHER_LON, 4);
    url += "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,precipitation,wind_speed_10m,wind_direction_10m,surface_pressure";
    url += "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,precipitation_probability_max,wind_speed_10m_max";
    url += "&timezone=" + tz;
    url += "&forecast_days=3";
    url += "&temperature_unit=celsius";
    url += "&wind_speed_unit=kmh";
    url += "&precipitation_unit=mm";

    DBG_PRINTLN("[Weather] GET open-meteo");

    WiFiClientSecure client;
    client.setCACertBundle(x509_crt_bundle);
    client.setHandshakeTimeout(15000);

    HTTPClient http;
    http.setTimeout(12000);
    http.setConnectTimeout(10000);

    if (!http.begin(client, url)) {
        d.error = "HTTP begin failed";
        g_weather = d;
        g_fetching = false;
        return false;
    }

    int code = http.GET();
    if (code != 200) {
        d.error = "HTTP " + String(code);
        http.end();
        g_weather = d;
        g_fetching = false;
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        d.error = "JSON parse: " + String(err.c_str());
        g_weather = d;
        g_fetching = false;
        return false;
    }

    JsonObject cur = doc["current"];
    d.temp = cur["temperature_2m"] | -999.0f;
    d.feels = cur["apparent_temperature"] | -999.0f;
    d.humidity = cur["relative_humidity_2m"] | -1.0f;
    d.precip = cur["precipitation"] | 0.0f;
    d.wind = cur["wind_speed_10m"] | -1.0f;
    d.windDir = cur["wind_direction_10m"] | -1.0f;
    d.pressure = cur["surface_pressure"] | -1.0f;
    d.code = cur["weather_code"] | -1;

    JsonArray maxT = doc["daily"]["temperature_2m_max"];
    JsonArray minT = doc["daily"]["temperature_2m_min"];
    JsonArray wcode = doc["daily"]["weather_code"];
    JsonArray precip = doc["daily"]["precipitation_sum"];
    JsonArray pop = doc["daily"]["precipitation_probability_max"];
    JsonArray wind = doc["daily"]["wind_speed_10m_max"];

    for (int i = 0; i < 3; i++) {
        d.days[i].code = wcode[i] | -1;
        d.days[i].maxTemp = maxT[i] | -999.0f;
        d.days[i].minTemp = minT[i] | -999.0f;
        d.days[i].precip = precip[i] | 0.0f;
        d.days[i].pop = pop[i] | -1.0f;
        d.days[i].wind = wind[i] | -1.0f;
    }

    d.ok = true;
    d.error = "";
    g_weather = d;
    g_hasData = true;
    g_fetchedAt = millis();
    g_fetching = false;
    DBG_PRINTLN("[Weather] ok temp=" + String(d.temp) + "C");
    return true;
}

bool weather_refresh_if_stale() {
    if (!g_hasData || millis() - g_fetchedAt > WEATHER_REFRESH_MS) {
        return weather_fetch();
    }
    return g_hasData;
}
