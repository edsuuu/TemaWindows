#pragma once

#include "wallpaper/audio.h"
#include "wallpaper/media.h"
#include "wallpaper/network.h"
#include "wallpaper/sensors.h"

#include <d2d1_1.h>
#include <dwrite.h>
#include <string>

struct CornerLayout {
    float right;
    float top;
    float cardLeft;
    float textLeft;
    float ringRadius;
    float scale;
};

constexpr float kPausedGrace = 3;

D2D1_RECT_F MediaButtonRect(CornerLayout const& layout, int button);

struct NowPlayingCard {
    float alpha = 0;
    bool showing = false;

    void Create(IDWriteFactory* dwrite, float scale);
    void Update(ID2D1DeviceContext* dc, IDWriteFactory* dwrite, NowPlaying const& song, CornerLayout const& layout, float dt);
    void Draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, CornerLayout const& layout, Equalizer const& equalizer, float visibility,
              int hovered, int pressed);

private:
    ComPtr<IDWriteTextFormat> titleFont;
    ComPtr<IDWriteTextFormat> artistFont;
    ComPtr<IDWriteTextFormat> iconFont;
    ComPtr<IDWriteTextFormat> elapsedFont;
    ComPtr<IDWriteTextFormat> totalFont;
    ComPtr<IDWriteTextLayout> titleLayout;
    ComPtr<IDWriteTextLayout> artistLayout;
    ComPtr<ID2D1BitmapBrush> coverBrush;
    std::shared_ptr<std::vector<BYTE>> cover;
    std::wstring title;
    std::wstring artist;
    float coverAlpha = 0;
    float pausedFor = kPausedGrace;
    float artistWidth = 0;
    float totalWidth = 0;
    std::wstring totalText;
    double position = 0;
    double duration = 0;
    double readAt = 0;
    bool playing = false;

    void DrawButtons(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, CornerLayout const& layout, float visibility, int hovered,
                     int pressed);
    void DrawProgress(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, CornerLayout const& layout, float visibility);
};

struct Rings {
    void Create(ID2D1Factory* factory, IDWriteFactory* dwrite, float scale);
    void Update(SystemStats const& stats, NetworkMeter const& network, double networkPercent);
    void Draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, ID2D1StrokeStyle* round, D2D1::Matrix3x2F const& corner,
              CornerLayout const& layout, float centerY);

private:
    ComPtr<ID2D1Factory> factory;
    ComPtr<IDWriteTextFormat> valueFont;
    ComPtr<IDWriteTextFormat> labelFont;
    ComPtr<IDWriteTextFormat> detailFont;
    ComPtr<ID2D1PathGeometry> arcs[4];
    std::wstring values[4];
    std::wstring details[4];
    float radius = 0;
    float scale = 1;

    ComPtr<ID2D1PathGeometry> Arc(double percent) const;
};
