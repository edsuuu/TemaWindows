#include "wallpaper/nbhd_theme.h"

#include <d2d1effects.h>
#include <algorithm>
#include <cmath>
#include <initializer_list>

#pragma comment(lib, "dxguid.lib")

struct Glyph {
    char character;
    int columns;
    const char* cells;
};

static const Glyph kFont[] = {
    {' ', 3, "..." "..." "..." "..." "..." "..." "..."},
    {'>', 4, "#..." ".#.." "..#." "...#" "..#." ".#.." "#..."},
    {'S', 5, ".####" "#...." "#...." ".###." "....#" "....#" "####."},
    {'T', 5, "#####" "..#.." "..#.." "..#.." "..#.." "..#.." "..#.."},
    {'A', 5, ".###." "#...#" "#...#" "#####" "#...#" "#...#" "#...#"},
    {'R', 5, "####." "#...#" "#...#" "####." "#.#.." "#..#." "#...#"},
    {'?', 5, ".###." "#...#" "....#" "..##." "..#.." "....." "..#.."},
    {'H', 5, "#...#" "#...#" "#...#" "#####" "#...#" "#...#" "#...#"},
    {'E', 5, "#####" "#...." "#...." "####." "#...." "#...." "#####"},
    {'N', 5, "#...#" "##..#" "#.#.#" "#..##" "#...#" "#...#" "#...#"},
    {'B', 5, "####." "#...#" "#...#" "####." "#...#" "#...#" "####."},
    {'D', 5, "####." "#...#" "#...#" "#...#" "#...#" "#...#" "####."},
    {'.', 1, "." "." "." "." "." "." "#"},
};

// Letra da fonte de blocos 5x7 desenhada aqui (nada de baixar fonte), só as que o tema usa; desconhecida vira espaço.
static const Glyph* FindGlyph(char character) {
    for (auto& glyph : kFont)
        if (glyph.character == character) return &glyph;
    return &kFont[0];
}

// "Ruído" fixo por bloco, de -0,5 a 0,5.
static float Jitter(unsigned block, unsigned i) {
    unsigned h = (block * 7919u + i) * 2654435761u;
    h ^= h >> 15;
    return (h % 1000) / 1000.f - 0.5f;
}

// Escreve em blocos, centralizado em (cx, cy): letras de 7 células de altura `cellHeight`, células `tall` vezes mais
// altas que largas. Os blocos passam da célula (traço grosso, sem emenda) e são levemente tortos, como na arte original.
// Devolve a caixa do primeiro caractere (o ">" que pisca).
static D2D1_RECT_F BlockText(ID2D1DeviceContext* dc, ID2D1Brush* brush, const char* text, float cx, float cy, float cellHeight, float tall) {
    const float cellWidth = cellHeight / tall;
    int columns = -1;
    for (const char* p = text; *p; p++) columns += FindGlyph(*p)->columns + 1;

    float x = cx - columns * cellWidth / 2;
    const float top = cy - 3.5f * cellHeight;
    D2D1_RECT_F first{x, top, x + FindGlyph(*text)->columns * cellWidth, top + 7 * cellHeight};
    unsigned block = 0;

    for (const char* p = text; *p; p++) {
        const Glyph* glyph = FindGlyph(*p);
        for (int row = 0; row < 7; row++)
            for (int column = 0; column < glyph->columns; column++, block++) {
                if (glyph->cells[row * glyph->columns + column] != '#') continue;

                float bx = x + (column - 0.25f + Jitter(block, 1) * 0.1f) * cellWidth;
                float by = top + (row - 0.15f + Jitter(block, 2) * 0.08f) * cellHeight;
                float bw = cellWidth * (1.5f + Jitter(block, 3) * 0.1f), bh = cellHeight * (1.3f + Jitter(block, 4) * 0.08f);
                dc->FillRectangle({bx, by, bx + bw, by + bh}, brush);
            }
        x += (glyph->columns + 1) * cellWidth;
    }
    return first;
}

// Uma figura do logo, em larguras do corpo da casa a partir de (originX, originY).
static void AddFigure(ID2D1GeometrySink* sink, float originX, float originY, float unit, std::initializer_list<float> points, bool closed) {
    const float* p = points.begin();
    sink->BeginFigure({originX + p[0] * unit, originY + p[1] * unit}, D2D1_FIGURE_BEGIN_HOLLOW);
    for (p += 2; p != points.end(); p += 2) sink->AddLine({originX + p[0] * unit, originY + p[1] * unit});
    sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
}

// Logo do The Neighbourhood (a casa de cabeça para baixo) num círculo, só contornos. Coordenadas em larguras do corpo
// da casa (medidas na capa do álbum): corpo 1 x 0,6 (o chão é a linha do telhado), telhado apontando para baixo,
// chaminé, porta (batente e folha) e janela 2x2.
static void DrawHouseLogo(ID2D1Factory* factory, ID2D1DeviceContext* dc, ID2D1Brush* brush, float cx, float cy, float radius, float lineWidth) {
    dc->DrawEllipse({{cx, cy}, radius, radius}, brush, lineWidth);

    const float unit = radius / 0.95f, originX = cx - 0.48f * unit, originY = cy - 0.63f * unit;
    ComPtr<ID2D1PathGeometry> geometry;
    ComPtr<ID2D1GeometrySink> sink;
    factory->CreatePathGeometry(&geometry);
    geometry->Open(&sink);

    AddFigure(sink.Get(), originX, originY, unit, {0, 0.61f, 0, 0, 1, 0, 1, 0.61f}, false);
    AddFigure(sink.Get(), originX, originY, unit, {-0.18f, 0.61f, 1.14f, 0.61f, 0.49f, 1.26f}, true);
    AddFigure(sink.Get(), originX, originY, unit, {0.71f, 1.04f, 0.71f, 1.19f, 0.88f, 1.19f, 0.88f, 0.87f}, false);
    AddFigure(sink.Get(), originX, originY, unit, {0.13f, 0, 0.13f, 0.51f, 0.43f, 0.51f, 0.43f, 0}, false);
    AddFigure(sink.Get(), originX, originY, unit, {0.2f, 0.09f, 0.36f, 0.09f, 0.36f, 0.44f, 0.2f, 0.44f}, true);
    AddFigure(sink.Get(), originX, originY, unit, {0.55f, 0.2f, 0.86f, 0.2f, 0.86f, 0.51f, 0.55f, 0.51f}, true);
    for (float y : {0.23f, 0.37f})
        for (float x : {0.58f, 0.72f}) AddFigure(sink.Get(), originX, originY, unit, {x, y, x + 0.11f, y, x + 0.11f, y + 0.11f, x, y + 0.11f}, true);

    sink->Close();
    dc->DrawGeometry(geometry.Get(), brush, lineWidth);
}

// Nova sessão da janela: a arte é refeita no tamanho do monitor principal (escala pela altura).
void NbhdTheme::Reset(float monitorWidth, float monitorHeight) {
    Release();
    width = monitorWidth;
    height = monitorHeight;
    logoY = 0.434f * height;
    logoRadius = 0.17f * height;
    lineWidth = 0.0062f * height;
}

// Arte pronta?
bool NbhdTheme::Built() const {
    return art != nullptr;
}

// Solta a arte: ela só fica na memória neste tema.
void NbhdTheme::Release() {
    art = nullptr;
    blurredArt = nullptr;
}

// O brilho que corre pelo círculo: três arcos de 70°, 35° e 12° que terminam juntos no topo, em volta de (0, 0).
void NbhdTheme::BuildTrail(ID2D1Factory* factory) {
    for (int i = 0; i < 3; i++) {
        ComPtr<ID2D1GeometrySink> sink;
        float span = (i == 0 ? 70 : i == 1 ? 35 : 12) * 3.1415927f / 180;
        factory->CreatePathGeometry(&trail[i]);
        trail[i]->Open(&sink);
        sink->BeginFigure({-logoRadius * sinf(span), -logoRadius * cosf(span)}, D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddArc({{0, -logoRadius}, {logoRadius, logoRadius}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL});
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
    }
}

// Tela de start retrô, em vetor: "> START ?" e "THE NBHD." em blocos amarelos (#F5B418) e o logo num círculo. A arte
// parada vai para um bitmap (com scanlines finas: a cada 3 linhas, uma mais escura) e o brilho dela, borrado, para
// outro, uma vez só; por quadro só se desenham os dois com opacidades que mudam e o que anima.
void NbhdTheme::Build(ID2D1Factory* factory, ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush) {
    D2D1_BITMAP_PROPERTIES1 properties{{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96, 96, D2D1_BITMAP_OPTIONS_TARGET};
    dc->CreateBitmap({UINT(width), UINT(height)}, nullptr, 0, &properties, &art);
    dc->CreateBitmap({UINT(width), UINT(height)}, nullptr, 0, &properties, &blurredArt);

    dc->SetTarget(art.Get());
    dc->BeginDraw();
    dc->Clear({0, 0, 0, 0});
    brush->SetColor({0.96f, 0.706f, 0.094f, 1});
    arrow = BlockText(dc, brush, "> START ?", width / 2, 0.162f * height, 0.012f * height, 1.5f);
    BlockText(dc, brush, "THE NBHD.", width / 2, 0.732f * height, 0.028f * height, 1.75f);
    brush->SetColor({0.93f, 0.93f, 0.93f, 1});
    DrawHouseLogo(factory, dc, brush, width / 2, logoY, logoRadius, lineWidth);
    dc->EndDraw();

    ComPtr<ID2D1Effect> blur;
    dc->CreateEffect(CLSID_D2D1GaussianBlur, &blur);
    blur->SetInput(0, art.Get());
    blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 0.012f * height);
    dc->SetTarget(blurredArt.Get());
    dc->BeginDraw();
    dc->Clear({0, 0, 0, 0});
    dc->DrawImage(blur.Get());
    dc->EndDraw();

    dc->SetTarget(art.Get());
    dc->BeginDraw();
    brush->SetColor({0, 0, 0, 0.35f});
    for (float y = 2; y < height; y += 3) dc->FillRectangle({0, y, width, y + 1}, brush);
    dc->EndDraw();
    dc->SetTarget(nullptr);
    BuildTrail(factory);
}

// A mesma arte em cada monitor (escala pela altura, centralizada). O amarelo tem um brilho que pulsa com os graves
// (sem música, respira devagar), um leve flicker de CRT e uma faixa clara descendo; o ">" pisca (tampado com preto,
// com o brilho em volta) e um brilho dá uma volta no círculo a cada 9 s.
void NbhdTheme::Draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, ID2D1StrokeStyle* round, std::vector<RECT> const& monitors,
                     float time, bool listening, float bass) {
    float glow = listening ? 0.35f + 0.6f * bass : 0.3f + 0.12f * (0.5f + 0.5f * sinf(time * 1.1f));
    float flicker = 0.95f + 0.05f * (0.5f + 0.5f * sinf(time * 53.f) * sinf(time * 17.3f));
    float band = fmodf(time / 8, 1) * 1.3f * height - 0.15f * height;
    float phase = fmodf(time, 1.2f), blink = std::clamp(std::min(phase, 0.75f - phase) / 0.06f + 0.5f, 0.f, 1.f);
    float spin = fmodf(time * 40, 360);

    for (auto& monitor : monitors) {
        float zoom = (monitor.bottom - monitor.top) / height;
        auto place = D2D1::Matrix3x2F::Scale(zoom, zoom) *
                     D2D1::Matrix3x2F::Translation(monitor.left + ((monitor.right - monitor.left) - width * zoom) / 2, float(monitor.top));
        dc->SetTransform(place);

        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        dc->DrawBitmap(blurredArt.Get(), nullptr, glow, D2D1_INTERPOLATION_MODE_LINEAR);
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
        dc->DrawBitmap(art.Get(), nullptr, flicker, D2D1_INTERPOLATION_MODE_LINEAR);

        dc->PushAxisAlignedClip({0, band, width, band + 0.1f * height}, D2D1_ANTIALIAS_MODE_ALIASED);
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        dc->DrawBitmap(art.Get(), nullptr, 0.12f, D2D1_INTERPOLATION_MODE_LINEAR);
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
        dc->PopAxisAlignedClip();

        if (blink < 1) {
            brush->SetColor({0, 0, 0, 1 - blink});
            float margin = 0.03f * height;
            dc->FillRectangle({arrow.left - margin, arrow.top - margin, arrow.right + margin, arrow.bottom + margin}, brush);
        }

        dc->SetTransform(D2D1::Matrix3x2F::Rotation(spin) * D2D1::Matrix3x2F::Translation(width / 2, logoY) * place);
        for (int i = 0; i < 3; i++) {
            brush->SetColor({1, 1, 1, i == 0 ? 0.25f : i == 1 ? 0.45f : 0.9f});
            dc->DrawGeometry(trail[i].Get(), brush, lineWidth * (i == 2 ? 1.4f : 1.15f), round);
        }
    }
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
}
