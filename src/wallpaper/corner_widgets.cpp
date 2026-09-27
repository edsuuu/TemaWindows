#include "wallpaper/corner_widgets.h"
#include "wallpaper/text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

constexpr float kMediaButton = 30;

// Fonte de uma linha com "…" no fim quando o texto não cabe.
static ComPtr<IDWriteTextFormat> TrimmedFont(IDWriteFactory* dwrite, const wchar_t* family, float pixels, DWRITE_FONT_WEIGHT weight) {
    auto font = SingleLineFont(dwrite, family, pixels, weight);
    ComPtr<IDWriteInlineObject> ellipsis;
    DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER};

    dwrite->CreateEllipsisTrimmingSign(font.Get(), &ellipsis);
    font->SetTrimming(&trimming, ellipsis.Get());
    return font;
}

// Botão do cartão (0 voltar, 1 tocar/pausar, 2 avançar): quadrados de 30 DIPs na linha do título, colados na borda
// direita.
D2D1_RECT_F MediaButtonRect(CornerLayout const& layout, int button) {
    const float s = layout.scale, size = kMediaButton * s, top = roundf(layout.top + 2 * s) - 2 * s, left = layout.right - (3 - button) * size;
    return {left, top, left + size, top + size};
}

// Fontes do título, do artista e dos ícones dos botões.
void NowPlayingCard::Create(IDWriteFactory* dwrite, float scale) {
    titleFont = TrimmedFont(dwrite, L"Segoe UI Variable Display", 21 * scale, DWRITE_FONT_WEIGHT_SEMI_LIGHT);
    artistFont = TrimmedFont(dwrite, L"Segoe UI Variable Text", 13.5f * scale, DWRITE_FONT_WEIGHT_NORMAL);
    iconFont = SingleLineFont(dwrite, L"Segoe Fluent Icons", 14 * scale);
    iconFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    iconFont->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

// O cartão aparece e some devagar (0,5 s): fica enquanto a música toca e mais 10 s depois de pausar (dá tempo de
// apertar play de novo). Na troca de música some, troca o texto (e a capa) e volta. A capa desta música entra com
// fade quando chega. O título deixa espaço para os botões.
void NowPlayingCard::Update(ID2D1DeviceContext* dc, IDWriteFactory* dwrite, NowPlaying const& song, CornerLayout const& layout, float dt) {
    bool changed = song.title != title || song.artist != artist;
    playing = song.playing;
    pausedFor = playing ? 0 : pausedFor + dt;
    showing = !song.title.empty() && pausedFor < kPausedGrace;
    alpha = std::clamp(alpha + (showing && !changed ? 2 : -2) * dt, 0.f, 1.f);

    if (changed && alpha == 0) {
        float width = layout.right - layout.textLeft;
        title = song.title;
        artist = song.artist;
        dwrite->CreateTextLayout(title.c_str(), (UINT)title.size(), titleFont.Get(), width - (3 * kMediaButton + 6) * layout.scale,
                                 30 * layout.scale, &titleLayout);
        dwrite->CreateTextLayout(artist.c_str(), (UINT)artist.size(), artistFont.Get(), width, 22 * layout.scale, &artistLayout);
        cover = nullptr;
        coverBrush = nullptr;
    }

    if (!changed && song.cover != cover) {
        int size = CoverSize();
        ComPtr<ID2D1Bitmap> bitmap;
        cover = song.cover;
        coverBrush = nullptr;
        coverAlpha = 0;
        if (cover && SUCCEEDED(dc->CreateBitmap({UINT(size), UINT(size)}, cover->data(), size * 4,
                                                {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96, 96}, &bitmap)))
            dc->CreateBitmapBrush(bitmap.Get(), &coverBrush);
    }

    coverAlpha = std::min(coverAlpha + 2 * dt, 1.f);
}

// Botões ⏮ ⏯ ⏭ (ícones da Segoe Fluent Icons): o que está sob o mouse ganha um fundo claro arredondado, mais claro
// enquanto apertado. O do meio mostra pausa tocando e play pausado.
void NowPlayingCard::DrawButtons(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, CornerLayout const& layout, float visibility, int hovered,
                                 int pressed) {
    const wchar_t* glyphs[3] = {L"\xE892", playing ? L"\xE769" : L"\xE768", L"\xE893"};

    for (int i = 0; i < 3; i++) {
        D2D1_RECT_F box = MediaButtonRect(layout, i);
        if (i == hovered) {
            D2D1_ROUNDED_RECT plate{box, 6 * layout.scale, 6 * layout.scale};
            brush->SetColor({1, 1, 1, (i == pressed ? 0.16f : 0.09f) * visibility});
            dc->FillRoundedRectangle(plate, brush);
        }
        brush->SetColor({0.92f, 0.92f, 0.92f, 0.95f * visibility});
        dc->DrawText(glyphs[i], 1, iconFont.Get(), box, brush);
    }
}

// Cartão de largura fixa no canto: capa à esquerda (cantos levemente arredondados; enquanto não chega, um quadrado bem
// apagado no lugar), título (~#F2F2F2) e artista (~#C8C8C8) colados nela até a borda direita, os botões na linha do
// título e o equalizador na base da capa, de ponta a ponta, com graves à esquerda e agudos à direita.
void NowPlayingCard::Draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, CornerLayout const& layout, Equalizer const& equalizer,
                          float visibility, int hovered, int pressed) {
    const float s = layout.scale, top = roundf(layout.top + 2 * s), size = float(CoverSize());
    const float left = layout.textLeft, width = layout.right - layout.textLeft;

    brush->SetColor({0.95f, 0.95f, 0.95f, 0.97f * visibility});
    dc->DrawTextLayout({left, top - 2 * s}, titleLayout.Get(), brush);
    brush->SetColor({0.78f, 0.78f, 0.78f, 0.95f * visibility});
    dc->DrawTextLayout({left, top + 28 * s}, artistLayout.Get(), brush);
    DrawButtons(dc, brush, layout, visibility, hovered, pressed);

    const float barWidth = roundf(width / Equalizer::kBands * 0.5f), step = (width - barWidth) / (Equalizer::kBands - 1), base = top + size;
    for (int i = 0; i < Equalizer::kBands; i++) {
        float height = (2 + 30 * equalizer.level[i]) * s, x = roundf(left + i * step);
        brush->SetColor({0.9f, 0.9f, 0.9f, (0.6f + 0.4f * equalizer.level[i]) * visibility});
        dc->FillRectangle({x, base - height, x + barWidth, base}, brush);
    }

    D2D1_ROUNDED_RECT frame{{layout.cardLeft, top, layout.cardLeft + size, top + size}, 6 * s, 6 * s};
    brush->SetColor({1, 1, 1, 0.05f * visibility * (1 - (coverBrush ? coverAlpha : 0))});
    dc->FillRoundedRectangle(frame, brush);
    if (coverBrush) {
        coverBrush->SetTransform(D2D1::Matrix3x2F::Translation(layout.cardLeft, top));
        coverBrush->SetOpacity(visibility * coverAlpha);
        dc->FillRoundedRectangle(frame, coverBrush.Get());
    }
}

// Fontes dos anéis (centralizadas): % em cima, temperatura/GB/↑ embaixo e o nome.
void Rings::Create(ID2D1Factory* d2dFactory, IDWriteFactory* dwrite, float dpiScale) {
    factory = d2dFactory;
    scale = dpiScale;
    radius = 36 * scale;
    valueFont = SingleLineFont(dwrite, L"Segoe UI Variable Display", 18 * scale);
    labelFont = SingleLineFont(dwrite, L"Segoe UI Variable Text", 11 * scale);
    detailFont = SingleLineFont(dwrite, L"Segoe UI Variable Text", 12 * scale);

    for (auto* font : {valueFont.Get(), labelFont.Get(), detailFont.Get()}) {
        font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        font->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

// Arco de 0 a 100% saindo do topo no sentido horário, em volta de (0, 0).
ComPtr<ID2D1PathGeometry> Rings::Arc(double percent) const {
    ComPtr<ID2D1PathGeometry> arc;
    ComPtr<ID2D1GeometrySink> sink;
    float fraction = (float)std::clamp(percent, 0.5, 99.9) / 100, angle = fraction * 6.2831853f;

    factory->CreatePathGeometry(&arc);
    arc->Open(&sink);
    sink->BeginFigure({0, -radius}, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc({{radius * sinf(angle), -radius * cosf(angle)}, {radius, radius}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                  fraction > 0.5f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL});
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    return arc;
}

// Temperatura em °C ou "--°" sem leitura.
static std::wstring Temperature(double celsius) {
    return celsius < 0 ? L"--°" : std::to_wstring(lround(celsius)) + L" °C";
}

// Novos valores (a cada 1,5 s): CPU, RAM, GPU e rede. Dentro de cada anel a % em cima e, menor, a temperatura (CPU e
// GPU), quanto da RAM está em uso ou, na rede, ↓ em cima e ↑ embaixo.
void Rings::Update(SystemStats const& stats, NetworkMeter const& network, double networkPercent) {
    double percents[4] = {stats.cpu, stats.ram, stats.gpu, networkPercent};
    for (int i = 0; i < 4; i++) {
        arcs[i] = Arc(percents[i]);
        values[i] = std::to_wstring(lround(percents[i])) + L"%";
    }

    wchar_t ram[16];
    swprintf_s(ram, L"%.1f GB", stats.ramGb);
    for (auto& c : ram)
        if (c == L'.') c = L',';

    values[3] = network.down;
    details[3] = network.up;
    details[0] = Temperature(stats.cpuTemperature);
    details[1] = ram;
    details[2] = Temperature(stats.gpuTemperature);
}

// Os quatro anéis juntos, centralizados embaixo do cartão da música, com o nome embaixo de cada um.
void Rings::Draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, ID2D1StrokeStyle* round, D2D1::Matrix3x2F const& corner,
                 CornerLayout const& layout, float centerY) {
    const wchar_t* labels[4] = {L"CPU", L"RAM", L"GPU", L"Mbps"};
    const float s = scale, r = radius;

    for (int i = 0; i < 4; i++) {
        float cx = roundf((layout.cardLeft + layout.right) / 2 + (i - 1.5f) * (2 * r + 20 * s));
        brush->SetColor({1, 1, 1, 0.18f});
        dc->DrawEllipse({{cx, centerY}, r, r}, brush, 3 * s);

        brush->SetColor({0.95f, 0.95f, 0.95f, 0.97f});
        dc->SetTransform(D2D1::Matrix3x2F::Translation(cx, centerY) * corner);
        dc->DrawGeometry(arcs[i].Get(), brush, 3 * s, round);
        dc->SetTransform(corner);
        dc->DrawText(values[i].c_str(), (UINT)values[i].size(), valueFont.Get(), {cx - r, centerY - 20 * s, cx + r, centerY + 4 * s}, brush);

        brush->SetColor({0.82f, 0.82f, 0.82f, 0.95f});
        dc->DrawText(details[i].c_str(), (UINT)details[i].size(), detailFont.Get(), {cx - r, centerY + 3 * s, cx + r, centerY + 19 * s}, brush);
        brush->SetColor({0.69f, 0.69f, 0.69f, 0.95f});
        dc->DrawText(labels[i], (UINT)wcslen(labels[i]), labelFont.Get(), {cx - r, centerY + r + 5 * s, cx + r, centerY + r + 20 * s}, brush);
    }
}
