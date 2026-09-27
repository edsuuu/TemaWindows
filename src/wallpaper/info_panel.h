#pragma once

#include "wallpaper/pc_specs.h"
#include "wallpaper/weather.h"
#include "wallpaper/weather_icon.h"

#include <dwrite.h>
#include <string>

struct InfoPanel {
    void Draw(ID2D1DeviceContext* screen, float x, float y, float width);
    float Height() const;

private:
    ComPtr<ID2D1DeviceContext> dc;
    ComPtr<ID2D1Factory> factory;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<ID2D1SolidColorBrush> jsonColors[4];
    ComPtr<IDWriteTextFormat> temperatureFont;
    ComPtr<IDWriteTextFormat> skyFont;
    ComPtr<IDWriteTextFormat> rangeFont;
    ComPtr<IDWriteTextFormat> codeFont;
    ComPtr<ID2D1StrokeStyle> round;
    ComPtr<ID2D1Bitmap1> bitmap;
    WeatherIcons icons;
    PcSpecs specs;
    Weather weather;
    std::wstring key;
    int bitmapWidth = 0;
    int bitmapHeight = 0;
    int second = -1;
    int weatherVersion = -1;
    float scale = 1;

    void Create(ID2D1DeviceContext* screen);
    void Render(SYSTEMTIME const& now, int width);
    void DrawWeatherColumn(float center, float halfWidth, float top, std::wstring const& temperature, std::wstring const& sky,
                           std::wstring const& range);
    void PaintText(std::wstring const& text, IDWriteTextFormat* font, D2D1_RECT_F const& box, float gray, float alpha);
};
