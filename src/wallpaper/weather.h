#pragma once

#include <windows.h>
#include <cmath>
#include <string>

struct Weather {
    double temperature = NAN;
    double maximum = NAN;
    double minimum = NAN;
    int code = -1;
    bool day = true;
};

void StartWeatherService();
int WeatherVersion();
std::string WeatherJson();
Weather ParseWeather(std::string const& json, SYSTEMTIME const& today);
const wchar_t* SkyText(int code);
