#pragma once

#include <windows.h>
#include <d2d1_1.h>
#include <wrl/client.h>
#include <vector>

using Microsoft::WRL::ComPtr;

struct NbhdTheme {
    void Reset(float width, float height);
    bool Built() const;
    void Build(ID2D1Factory* factory, ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush);
    void Release();
    void Draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, ID2D1StrokeStyle* round, std::vector<RECT> const& monitors, float time,
              bool listening, float bass);

private:
    ComPtr<ID2D1Bitmap1> art;
    ComPtr<ID2D1Bitmap1> blurredArt;
    ComPtr<ID2D1PathGeometry> trail[3];
    D2D1_RECT_F arrow{};
    float width = 0;
    float height = 0;
    float logoY = 0;
    float logoRadius = 0;
    float lineWidth = 0;

    void BuildTrail(ID2D1Factory* factory);
};
