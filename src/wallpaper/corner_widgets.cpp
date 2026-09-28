#include "wallpaper/corner_widgets.h"
#include "wallpaper/text.h"
#include "wallpaper/timing.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

constexpr float kMediaButton = 30;
constexpr float kProgressMin = 70;
constexpr float kProgressGap = 14;
constexpr float kTimeGap = 8;
constexpr float kTimeReserve = 30;
constexpr float kNextGap = 12;
constexpr float kNextRow = 26;
constexpr float kMarqueeSpeed = 30;
constexpr float kMarqueeGap = 48;
constexpr float kMarqueeFade = 16;
constexpr float kUnbounded = 10000;
constexpr double kMarqueePause = 2.5;

// Largura natural de um texto (layout sem limite).
static float LayoutWidth(IDWriteTextLayout* layout) {
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return metrics.width;
}

// Tempo da música: "0:42", "3:05" ou, passando de uma hora, "1:02:05".
static std::wstring FormatTime(double seconds) {
    int total = int(seconds), hours = total / 3600, minutes = total / 60 % 60, rest = total % 60;
    wchar_t text[16];
    if (hours) swprintf_s(text, L"%d:%02d:%02d", hours, minutes, rest);
    else swprintf_s(text, L"%d:%02d", minutes, rest);
    return text;
}

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
    elapsedFont = SingleLineFont(dwrite, L"Segoe UI Variable Text", 11 * scale);
    elapsedFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    elapsedFont->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    totalFont = SingleLineFont(dwrite, L"Segoe UI Variable Text", 11 * scale);
    totalFont->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    nextTitleFont = TrimmedFont(dwrite, L"Segoe UI Variable Text", 12 * scale, DWRITE_FONT_WEIGHT_NORMAL);
    nextArtistFont = TrimmedFont(dwrite, L"Segoe UI Variable Text", 10.5f * scale, DWRITE_FONT_WEIGHT_NORMAL);
}

// Degradês das pontas do carrossel (transparente -> opaco), criados uma vez; os pontos mudam a cada desenho.
void NowPlayingCard::CreateFades(ID2D1DeviceContext* dc) {
    if (fadeLeft) return;

    D2D1_GRADIENT_STOP stops[2] = {{0, {1, 1, 1, 0}}, {1, {1, 1, 1, 1}}};
    ComPtr<ID2D1GradientStopCollection> collection;
    dc->CreateGradientStopCollection(stops, 2, &collection);
    dc->CreateLinearGradientBrush({}, collection.Get(), &fadeLeft);
    dc->CreateLinearGradientBrush({}, collection.Get(), &fadeRight);
}

// Texto de uma linha que, se não couber em `width`, roda como carrossel da direita para a esquerda: parado 2,5 s no
// começo, anda até a cópia seguinte chegar no lugar e recomeça. As pontas somem num degradê; o da esquerda cresce
// quando o texto sai andando e encolhe quando a cópia chega, para o começo parado ficar nítido.
void NowPlayingCard::DrawMarquee(ID2D1DeviceContext* dc, ID2D1Brush* brush, IDWriteTextLayout* text, float textWidth, D2D1_POINT_2F at,
                                 float width, float height, float s, double since) {
    if (textWidth <= width) {
        dc->DrawTextLayout(at, text, brush);
        return;
    }

    const float cycle = textWidth + kMarqueeGap * s;
    double moving = cycle / (kMarqueeSpeed * s), t = fmod(Now() - since, kMarqueePause + moving);
    float offset = t < kMarqueePause ? 0 : float(t - kMarqueePause) * kMarqueeSpeed * s;
    float leftFade = std::max(std::min({offset, cycle - offset, kMarqueeFade * s}), 0.01f);
    D2D1_RECT_F clip{at.x, at.y, at.x + width, at.y + height};

    fadeRight->SetStartPoint({clip.right, 0});
    fadeRight->SetEndPoint({clip.right - kMarqueeFade * s, 0});
    fadeLeft->SetStartPoint({clip.left, 0});
    fadeLeft->SetEndPoint({clip.left + leftFade, 0});
    dc->PushLayer(D2D1::LayerParameters1(clip, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 1, fadeRight.Get()), nullptr);
    dc->PushLayer(D2D1::LayerParameters1(clip, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 1, fadeLeft.Get()), nullptr);
    dc->DrawTextLayout({at.x - offset, at.y}, text, brush);
    dc->DrawTextLayout({at.x - offset + cycle, at.y}, text, brush);
    dc->PopLayer();
    dc->PopLayer();
}

// Quanto a linha da próxima música empurra os anéis para baixo (acompanha o fade dela).
float NowPlayingCard::NextRowHeight(float scale) const {
    return nextAlpha * nextAlpha * (3 - 2 * nextAlpha) * (kNextGap + kNextRow) * scale;
}

// Linha da próxima música (capa pequena, nome e banda): aparece com fade quando a fila lida é da música que está no
// cartão (o Spotify informa qual tocava) e some antes de trocar o texto. A capa entra quando chega.
void NowPlayingCard::UpdateNext(ID2D1DeviceContext* dc, IDWriteFactory* dwrite, NextTrack const& next, CornerLayout const& layout, float dt) {
    const float s = layout.scale;
    std::wstring key = next.title + L"\n" + next.artist;
    bool valid = showing && !next.title.empty() && next.after == title;

    if (key != nextKey && nextAlpha == 0) {
        nextKey = key;
        dwrite->CreateTextLayout(next.title.c_str(), (UINT)next.title.size(), nextTitleFont.Get(), kUnbounded, 16 * s, &nextTitleLayout);
        nextTitleWidth = LayoutWidth(nextTitleLayout.Get());
        nextSince = Now();
        dwrite->CreateTextLayout(next.artist.c_str(), (UINT)next.artist.size(), nextArtistFont.Get(), kUnbounded, 14 * s, &nextArtistLayout);
        nextArtistWidth = LayoutWidth(nextArtistLayout.Get());
        nextCover = nullptr;
        nextCoverBrush = nullptr;
    }

    if (key == nextKey && next.cover != nextCover) {
        int size = NextCoverSize();
        ComPtr<ID2D1Bitmap> bitmap;
        nextCover = next.cover;
        nextCoverBrush = nullptr;
        if (nextCover && SUCCEEDED(dc->CreateBitmap({UINT(size), UINT(size)}, nextCover->data(), size * 4,
                                                    {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96, 96}, &bitmap)))
            dc->CreateBitmapBrush(bitmap.Get(), &nextCoverBrush);
    }

    nextAlpha = std::clamp(nextAlpha + (valid && key == nextKey ? 3 : -3) * dt, 0.f, 1.f);
}

// Desenha a linha da próxima música embaixo da capa: capa de 26 DIPs (um quadrado apagado enquanto não chega), nome
// (~#D9D9D9) e banda (~#999999) em letra pequena.
void NowPlayingCard::DrawNext(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, CornerLayout const& layout, float visibility) {
    float alpha = visibility * nextAlpha;
    if (alpha <= 0) return;

    const float s = layout.scale, size = float(NextCoverSize()), top = roundf(layout.top + 2 * s) + CoverSize() + kNextGap * s;
    const float x = layout.cardLeft + size + 10 * s;
    D2D1_ROUNDED_RECT frame{{layout.cardLeft, top, layout.cardLeft + size, top + size}, 4 * s, 4 * s};

    if (nextCoverBrush) {
        nextCoverBrush->SetTransform(D2D1::Matrix3x2F::Translation(layout.cardLeft, top));
        nextCoverBrush->SetOpacity(alpha);
        dc->FillRoundedRectangle(frame, nextCoverBrush.Get());
    } else {
        brush->SetColor({1, 1, 1, 0.06f * alpha});
        dc->FillRoundedRectangle(frame, brush);
    }

    brush->SetColor({0.85f, 0.85f, 0.85f, 0.95f * alpha});
    DrawMarquee(dc, brush, nextTitleLayout.Get(), nextTitleWidth, {x, top - 2 * s}, layout.right - x, 16 * s, s, nextSince);
    brush->SetColor({0.6f, 0.6f, 0.6f, 0.95f * alpha});
    DrawMarquee(dc, brush, nextArtistLayout.Get(), nextArtistWidth, {x, top + 12 * s}, layout.right - x, 14 * s, s, nextSince);
}

// O cartão aparece e some devagar (0,5 s): fica enquanto a música toca e mais 3 s depois de pausar (dá tempo de
// apertar play de novo). Na troca de música some, troca o texto (e a capa) e volta. A capa desta música entra com
// fade quando chega. O título deixa espaço para os botões e o artista, para a barra de progresso.
void NowPlayingCard::Update(ID2D1DeviceContext* dc, IDWriteFactory* dwrite, NowPlaying const& song, NextTrack const& next, CornerLayout const& layout,
                            float dt) {
    bool changed = song.title != title || song.artist != artist;
    playing = song.playing;
    position = song.position;
    readAt = song.readAt;
    if (song.duration != duration) {
        duration = song.duration;
        totalText = FormatTime(duration);
        totalWidth = TextWidth(dwrite, totalText, totalFont.Get());
    }
    pausedFor = playing ? 0 : pausedFor + dt;
    showing = !song.title.empty() && pausedFor < kPausedGrace;
    alpha = std::clamp(alpha + (showing && !changed ? 2 : -2) * dt, 0.f, 1.f);

    if (changed && alpha == 0) {
        float width = layout.right - layout.textLeft;
        title = song.title;
        artist = song.artist;
        dwrite->CreateTextLayout(title.c_str(), (UINT)title.size(), titleFont.Get(), kUnbounded, 30 * layout.scale, &titleLayout);
        titleWidth = LayoutWidth(titleLayout.Get());
        titleSince = Now();
        dwrite->CreateTextLayout(artist.c_str(), (UINT)artist.size(), artistFont.Get(), kUnbounded, 22 * layout.scale, &artistLayout);
        artistWidth = LayoutWidth(artistLayout.Get());
        artistMax = width - (kProgressGap + 2 * (kTimeReserve + kTimeGap) + kProgressMin) * layout.scale;
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
    UpdateNext(dc, dwrite, next, layout, dt);
    CreateFades(dc);
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

// Barra de progresso da música na linha do artista, do fim do nome até a borda direita: tempo decorrido, trilho
// apagado com o preenchimento claro por cima (cantos redondos) e a duração. Os dois tempos ficam em caixas da largura
// da duração (o decorrido nunca passa dela), então a barra não pula. Entre uma leitura e outra (1 s) a posição anda
// com o relógio, no máximo 2 s (leitura velha não adianta o tempo).
void NowPlayingCard::DrawProgress(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, CornerLayout const& layout, float visibility) {
    if (duration <= 0) return;

    const float s = layout.scale, y = roundf(layout.top + 2 * s) + 39 * s, start = roundf(layout.textLeft + std::min(artistWidth, artistMax) + kProgressGap * s);
    double elapsed = std::clamp(position + (playing ? std::min(Now() - readAt, 2.0) : 0), 0.0, duration);
    std::wstring elapsedText = FormatTime(elapsed);
    D2D1_RECT_F elapsedBox{start, y - 9 * s, start + totalWidth, y + 9 * s};
    D2D1_RECT_F totalBox{layout.right - totalWidth, y - 9 * s, layout.right, y + 9 * s};

    brush->SetColor({0.62f, 0.62f, 0.62f, 0.95f * visibility});
    dc->DrawText(elapsedText.c_str(), (UINT)elapsedText.size(), elapsedFont.Get(), elapsedBox, brush);
    dc->DrawText(totalText.c_str(), (UINT)totalText.size(), totalFont.Get(), totalBox, brush);

    const float left = elapsedBox.right + kTimeGap * s, right = totalBox.left - kTimeGap * s;
    float fraction = float(elapsed / duration);
    D2D1_ROUNDED_RECT bar{{left, y - 1.5f * s, right, y + 1.5f * s}, 1.5f * s, 1.5f * s};
    brush->SetColor({1, 1, 1, 0.16f * visibility});
    dc->FillRoundedRectangle(bar, brush);
    bar.rect.right = left + (right - left) * fraction;
    brush->SetColor({0.9f, 0.9f, 0.9f, 0.9f * visibility});
    dc->FillRoundedRectangle(bar, brush);
}

// Cartão de largura fixa no canto: capa à esquerda (cantos levemente arredondados; enquanto não chega, um quadrado bem
// apagado no lugar), título (~#F2F2F2) e artista (~#C8C8C8) colados nela até a borda direita, os botões na linha do
// título, a barra de progresso na do artista e o equalizador na base da capa, de ponta a ponta, com graves à esquerda
// e agudos à direita.
void NowPlayingCard::Draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, CornerLayout const& layout, Equalizer const& equalizer,
                          float visibility, int hovered, int pressed) {
    const float s = layout.scale, top = roundf(layout.top + 2 * s), size = float(CoverSize());
    const float left = layout.textLeft, width = layout.right - layout.textLeft;

    brush->SetColor({0.95f, 0.95f, 0.95f, 0.97f * visibility});
    DrawMarquee(dc, brush, titleLayout.Get(), titleWidth, {left, top - 2 * s}, width - (3 * kMediaButton + 6) * s, 30 * s, s, titleSince);
    brush->SetColor({0.78f, 0.78f, 0.78f, 0.95f * visibility});
    DrawMarquee(dc, brush, artistLayout.Get(), artistWidth, {left, top + 28 * s}, artistMax, 22 * s, s, titleSince);
    DrawButtons(dc, brush, layout, visibility, hovered, pressed);
    DrawProgress(dc, brush, layout, visibility);

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
    DrawNext(dc, brush, layout, visibility);
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
