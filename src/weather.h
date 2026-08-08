#pragma once
#include "config.h"

struct WeatherDay {
    int   code;
    float maxTemp;
    float minTemp;
    float precip;
    float pop;
    float wind;
};

struct WeatherData {
    bool   ok;
    String error;
    String city;

    float  temp;
    float  feels;
    float  humidity;
    float  precip;
    float  wind;
    float  windDir;
    float  pressure;
    int    code;

    WeatherDay days[3];
};

WeatherData weather_get();
bool weather_has_data();
bool weather_fetch();
bool weather_refresh_if_stale();
String weather_code_text(int code);
String weather_wind_dir(float deg);
