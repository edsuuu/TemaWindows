#include "wallpaper/weather_icon.h"

#include <cmath>
#include <initializer_list>

struct IconPen {
    ID2D1DeviceContext* dc;
    ID2D1SolidColorBrush* brush;
    ID2D1StrokeStyle* round;
    WeatherIcons const& icons;
    D2D1::Matrix3x2F base;
    D2D1_POINT_2F origin;
    float zoom;
    float lineWidth;
    bool day;
};

// Junta duas geometrias (união ou exclusão) numa geometria nova.
static ComPtr<ID2D1Geometry> Combine(ID2D1Factory* factory, ID2D1Geometry* a, ID2D1Geometry* b, D2D1_COMBINE_MODE mode) {
    ComPtr<ID2D1PathGeometry> result;
    ComPtr<ID2D1GeometrySink> sink;
    factory->CreatePathGeometry(&result);
    result->Open(&sink);
    a->CombineWithGeometry(b, mode, nullptr, sink.Get());
    sink->Close();
    return result;
}

// Formas dos ícones numa caixa de 64 x 64: nuvem = três círculos e a base reta; lua = um círculo menos outro.
void WeatherIcons::Create(ID2D1Factory* factory) {
    ComPtr<ID2D1EllipseGeometry> left, middle, right, moonDisc, moonBite;
    ComPtr<ID2D1RectangleGeometry> base;
    factory->CreateEllipseGeometry({{20, 40}, 10, 10}, &left);
    factory->CreateEllipseGeometry({{32, 31}, 13, 13}, &middle);
    factory->CreateEllipseGeometry({{44, 40}, 10, 10}, &right);
    factory->CreateRectangleGeometry({20, 40, 44, 50}, &base);
    factory->CreateEllipseGeometry({{28, 32}, 14, 14}, &moonDisc);
    factory->CreateEllipseGeometry({{36, 25}, 12, 12}, &moonBite);

    auto top = Combine(factory, left.Get(), middle.Get(), D2D1_COMBINE_MODE_UNION);
    auto puffs = Combine(factory, top.Get(), right.Get(), D2D1_COMBINE_MODE_UNION);
    cloud = Combine(factory, puffs.Get(), base.Get(), D2D1_COMBINE_MODE_UNION);
    moon = Combine(factory, moonDisc.Get(), moonBite.Get(), D2D1_COMBINE_MODE_EXCLUDE);
}

// Desenha as próximas formas em coordenadas da caixa de 64, depois da transformação `local`.
static void Place(IconPen const& pen, D2D1::Matrix3x2F const& local) {
    pen.dc->SetTransform(local * D2D1::Matrix3x2F::Scale(pen.zoom, pen.zoom) * D2D1::Matrix3x2F::Translation(pen.origin.x, pen.origin.y) * pen.base);
}

// Traço reto com pontas redondas, na transformação atual.
static void Line(IconPen const& pen, float x0, float y0, float x1, float y1) {
    pen.dc->DrawLine({x0, y0}, {x1, y1}, pen.brush, pen.lineWidth, pen.round);
}

// Sol (de noite, lua) centrado em (cx, cy) na caixa de 64, na escala `size`.
static void DrawSky(IconPen const& pen, float cx, float cy, float size) {
    Place(pen, D2D1::Matrix3x2F::Translation(-32, -32) * D2D1::Matrix3x2F::Scale(size, size) * D2D1::Matrix3x2F::Translation(cx, cy));
    if (!pen.day) {
        pen.dc->DrawGeometry(pen.icons.moon.Get(), pen.brush, pen.lineWidth / size, pen.round);
        return;
    }

    pen.dc->DrawEllipse({{32, 32}, 9, 9}, pen.brush, pen.lineWidth / size);
    for (int i = 0; i < 8; i++) {
        float angle = i * 0.785398f, c = cosf(angle), s = sinf(angle);
        pen.dc->DrawLine({32 + 14 * c, 32 + 14 * s}, {32 + 19 * c, 32 + 19 * s}, pen.brush, pen.lineWidth / size, pen.round);
    }
}

// Nuvem deslocada (dx, dy): primeiro apaga o que ficou atrás dela (o sol), depois desenha o contorno.
static void DrawCloud(IconPen const& pen, float dx, float dy) {
    Place(pen, D2D1::Matrix3x2F::Translation(dx, dy));
    auto color = pen.brush->GetColor();

    pen.dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
    pen.brush->SetColor({0, 0, 0, 0});
    pen.dc->FillGeometry(pen.icons.cloud.Get(), pen.brush);
    pen.dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
    pen.brush->SetColor(color);
    pen.dc->DrawGeometry(pen.icons.cloud.Get(), pen.brush, pen.lineWidth, pen.round);
    Place(pen, D2D1::Matrix3x2F::Identity());
}

// Embaixo da nuvem: raio (trovoada), gotinhas (garoa), flocos (neve) ou traços inclinados (chuva).
static void DrawPrecipitation(IconPen const& pen, int code) {
    if (code >= 95) {
        Line(pen, 35, 48, 29, 56);
        Line(pen, 29, 56, 36, 56);
        Line(pen, 36, 56, 30, 63);
    } else if (code < 60) {
        for (float x : {22.f, 32.f, 42.f}) pen.dc->FillEllipse({{x, 56}, 1.8f, 1.8f}, pen.brush);
    } else if ((code >= 70 && code < 80) || code >= 85) {
        for (float x : {22.f, 32.f, 42.f}) pen.dc->FillEllipse({{x, 54}, 1.8f, 1.8f}, pen.brush);
        for (float x : {27.f, 37.f}) pen.dc->FillEllipse({{x, 61}, 1.8f, 1.8f}, pen.brush);
    } else {
        for (float x : {25.f, 34.f, 43.f}) Line(pen, x, 52, x - 3, 60);
    }
}

// Ícone do clima (código WMO) em traço fino como os anéis, numa caixa size x size em `origin`; o desenho começa ~10/64
// para dentro da borda. Sol ou lua sozinhos, meio encobertos, nublado, neblina ou nuvem com precipitação.
void DrawWeatherIcon(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, ID2D1StrokeStyle* round, WeatherIcons const& icons,
                     D2D1_POINT_2F origin, float size, float scale, int code, bool day) {
    D2D1::Matrix3x2F base;
    dc->GetTransform(&base);
    float zoom = size / 64;
    IconPen pen{dc, brush, round, icons, base, origin, zoom, 2 * scale / zoom, day};
    brush->SetColor({0.93f, 0.93f, 0.93f, 0.95f});

    if (code == 0) {
        DrawSky(pen, 32, 32, 1);
    } else if (code <= 2) {
        DrawSky(pen, 23, 22, 0.72f);
        DrawCloud(pen, 6, 6);
    } else if (code == 3) {
        DrawCloud(pen, 0, 2);
    } else if (code < 50) {
        Place(pen, D2D1::Matrix3x2F::Identity());
        Line(pen, 14, 22, 50, 22);
        Line(pen, 8, 30, 56, 30);
        Line(pen, 14, 38, 50, 38);
        Line(pen, 20, 46, 44, 46);
    } else {
        DrawCloud(pen, 0, -4);
        DrawPrecipitation(pen, code);
    }
    dc->SetTransform(base);
}
