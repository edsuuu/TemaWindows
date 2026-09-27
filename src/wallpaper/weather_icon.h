#pragma once

#include <windows.h>
#include <d2d1_1.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct WeatherIcons {
    ComPtr<ID2D1Geometry> cloud;
    ComPtr<ID2D1Geometry> moon;

    void Create(ID2D1Factory* factory);
};

void DrawWeatherIcon(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, ID2D1StrokeStyle* round, WeatherIcons const& icons,
                     D2D1_POINT_2F origin, float size, float scale, int code, bool day);
