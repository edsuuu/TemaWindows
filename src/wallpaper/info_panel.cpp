#include "wallpaper/info_panel.h"
#include "wallpaper/desktop.h"
#include "wallpaper/text.h"

#include <algorithm>
#include <cstdio>
#include <tuple>
#include <vector>

constexpr float kGapBelowRings = 24;
constexpr float kLineHeight = 18;
constexpr float kIconSize = 48;
constexpr float kTemperatureRow = 42;
constexpr float kSkyRow = 20;
constexpr float kRangeRow = 18;

struct JsonText {
    std::wstring text;
    std::vector<std::tuple<UINT32, UINT32, int>> runs;
};

// swprintf para std::wstring.
template <class... Args>
static std::wstring Format(const wchar_t* format, Args... args) {
    wchar_t text[256];
    swprintf_s(text, format, args...);
    return text;
}

// String JSON: entre aspas, com aspas e barras escapadas.
static std::wstring JsonString(std::wstring value) {
    for (size_t p = 0; (p = value.find_first_of(L"\"\\", p)) != std::wstring::npos; p += 2) value.insert(p, 1, L'\\');
    return L"\"" + value + L"\"";
}

// Tempo ligado: "3min", "1h 12min" ou "2d 5h".
static std::wstring Uptime(ULONGLONG minutes) {
    if (minutes >= 1440) return Format(L"%llud %lluh", minutes / 1440, minutes / 60 % 24);
    if (minutes >= 60) return Format(L"%lluh %llumin", minutes / 60, minutes % 60);
    return Format(L"%llumin", minutes);
}

// O JSON do painel (uptime, specs do PC e monitores), com a cor de cada pedaço: 0 pontuação, 1 chave, 2 string.
static JsonText BuildJson(PcSpecs const& specs, std::vector<std::wstring> const& monitors, ULONGLONG minutes) {
    JsonText json;
    auto add = [&](std::wstring const& piece, int color) {
        json.runs.push_back({(UINT32)json.text.size(), (UINT32)piece.size(), color});
        json.text += piece;
    };
    auto field = [&](const wchar_t* indent, const wchar_t* key, std::wstring const& value, bool last = false) {
        add(indent, 0);
        add(L"\"" + std::wstring(key) + L"\"", 1);
        add(L": ", 0);
        add(value, 2);
        add(last ? L"\n" : L",\n", 0);
    };

    add(L"{\n", 0);
    field(L"  ", L"uptime", JsonString(Uptime(minutes)));
    add(L"  ", 0);
    add(L"\"pc\"", 1);
    add(L": {\n", 0);
    field(L"    ", L"cpu", JsonString(specs.cpu));
    field(L"    ", L"gpu", JsonString(specs.gpu));
    field(L"    ", L"ram", JsonString(specs.ram), monitors.empty());
    if (!monitors.empty()) {
        add(L"    ", 0);
        add(L"\"monitors\"", 1);
        add(L": [\n", 0);
        for (size_t i = 0; i < monitors.size(); i++) {
            add(L"      ", 0);
            add(JsonString(monitors[i]), 2);
            add(i + 1 < monitors.size() ? L",\n" : L"\n", 0);
        }
        add(L"    ]\n", 0);
    }
    add(L"  }\n", 0);
    add(L"}", 0);
    return json;
}

// Contexto próprio do Direct2D (o do quadro está no meio do desenho quando o bitmap é refeito), fontes, cores do JSON
// (pontuação apagada, chaves cinza médio, strings quase brancas, números brancos), ícones e as specs, lidas uma vez.
void InfoPanel::Create(ID2D1DeviceContext* screen) {
    ComPtr<ID2D1Device> device;
    screen->GetDevice(&device);
    device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc);
    dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    dc->GetFactory(&factory);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)dwrite.GetAddressOf());
    scale = DpiScale();

    dc->CreateSolidColorBrush({1, 1, 1, 1}, &brush);
    const D2D1_COLOR_F colors[4] = {{.46f, .46f, .46f, 1}, {.66f, .66f, .66f, 1}, {.90f, .90f, .90f, 1}, {1, 1, 1, 1}};
    for (int i = 0; i < 4; i++) dc->CreateSolidColorBrush(colors[i], &jsonColors[i]);

    temperatureFont = SingleLineFont(dwrite.Get(), L"Segoe UI Variable Display", 34 * scale, DWRITE_FONT_WEIGHT_SEMI_LIGHT);
    skyFont = SingleLineFont(dwrite.Get(), L"Segoe UI Variable Text", 13.5f * scale);
    rangeFont = SingleLineFont(dwrite.Get(), L"Segoe UI Variable Text", 12 * scale);
    for (auto* font : {temperatureFont.Get(), skyFont.Get(), rangeFont.Get()}) {
        font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        font->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    codeFont = SingleLineFont(dwrite.Get(), L"Cascadia Code", 12 * scale);
    codeFont->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, kLineHeight * scale, 14 * scale);

    factory->CreateStrokeStyle({D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND, 10, D2D1_DASH_STYLE_SOLID, 0},
                               nullptr, 0, &round);
    icons.Create(factory.Get());
    specs = ReadPcSpecs();
}

// Um texto em cinza `gray`, centralizado na caixa.
void InfoPanel::PaintText(std::wstring const& text, IDWriteTextFormat* font, D2D1_RECT_F const& box, float gray, float alpha) {
    brush->SetColor({gray, gray, gray, alpha});
    dc->DrawText(text.c_str(), (UINT)text.size(), font, box, brush.Get());
}

// Clima em coluna, centralizado em `center`: ícone em cima, a temperatura grande embaixo dele, depois o céu e a
// máx/mín.
void InfoPanel::DrawWeatherColumn(float center, float halfWidth, float top, std::wstring const& temperature, std::wstring const& sky,
                                  std::wstring const& range) {
    float left = center - halfWidth, right = center + halfWidth;
    float y = top + kIconSize * scale;

    DrawWeatherIcon(dc.Get(), brush.Get(), round.Get(), icons, {center - kIconSize * scale / 2, top}, kIconSize * scale, scale, weather.code,
                    weather.day);
    PaintText(temperature, temperatureFont.Get(), {left, y, right, y + kTemperatureRow * scale}, 0.95f, 0.97f);
    y += kTemperatureRow * scale;
    PaintText(sky, skyFont.Get(), {left, y, right, y + kSkyRow * scale}, 0.78f, 0.95f);
    y += kSkyRow * scale;
    PaintText(range, rangeFont.Get(), {left, y, right, y + kRangeRow * scale}, 0.69f, 0.95f);
}

// 1x por segundo: dados novos (monitores 1x por minuto, para pegar troca de resolução ou de Hz) e, se o texto mudou, o
// bitmap do bloco (largura = a dos anéis). O JSON fica à esquerda, rente aos anéis; o clima fica à direita dele, em
// coluna centralizada no espaço que sobra (sem passar da borda direita) e centralizada na altura do JSON.
void InfoPanel::Render(SYSTEMTIME const& now, int width) {
    if (WeatherVersion() != weatherVersion) {
        weatherVersion = WeatherVersion();
        weather = ParseWeather(WeatherJson(), now);
    }

    if (monitors.empty() || now.wSecond == 0) monitors = ReadMonitors();

    bool hasWeather = !std::isnan(weather.temperature) && weather.code >= 0;
    JsonText json = BuildJson(specs, monitors, GetTickCount64() / 60000);
    std::wstring temperature = hasWeather ? Format(L"%.0f°", weather.temperature) : L"";
    std::wstring sky = hasWeather ? SkyText(weather.code) : L"";
    std::wstring range = hasWeather && !std::isnan(weather.maximum) ? Format(L"máx %.0f°  ·  mín %.0f°", weather.maximum, weather.minimum) : L"";
    std::wstring newKey = json.text + temperature + sky + range + std::to_wstring(weather.code * 2 + weather.day) + std::to_wstring(width);
    if (bitmap && newKey == key) return;
    key = newKey;

    int lines = int(std::count(json.text.begin(), json.text.end(), L'\n')) + 1;
    float jsonHeight = lines * kLineHeight * scale;
    ComPtr<IDWriteTextLayout> layout;
    DWRITE_TEXT_METRICS metrics{};
    dwrite->CreateTextLayout(json.text.c_str(), (UINT)json.text.size(), codeFont.Get(), float(width), jsonHeight, &layout);
    for (auto [start, length, color] : json.runs) layout->SetDrawingEffect(jsonColors[color].Get(), {start, length});
    layout->GetMetrics(&metrics);

    float columnHeight = hasWeather ? (kIconSize + kTemperatureRow + kSkyRow + kRangeRow) * scale : 0;
    int height = int(std::max(jsonHeight, columnHeight));
    if (!bitmap || bitmapWidth != width || bitmapHeight != height) {
        D2D1_BITMAP_PROPERTIES1 properties{{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96, 96, D2D1_BITMAP_OPTIONS_TARGET};
        bitmap = nullptr;
        dc->CreateBitmap({UINT(width), UINT(height)}, nullptr, 0, &properties, &bitmap);
        bitmapWidth = width;
        bitmapHeight = height;
    }

    dc->SetTarget(bitmap.Get());
    dc->BeginDraw();
    dc->Clear({0, 0, 0, 0});
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    dc->DrawTextLayout({0, 0}, layout.Get(), jsonColors[0].Get());

    if (hasWeather) {
        float widest = std::max({kIconSize * scale, TextWidth(dwrite.Get(), temperature, temperatureFont.Get()),
                                 TextWidth(dwrite.Get(), sky, skyFont.Get()), TextWidth(dwrite.Get(), range, rangeFont.Get())});
        float center = std::min(metrics.width + (width - metrics.width) / 2, width - widest / 2);
        DrawWeatherColumn(std::round(center), widest / 2 + 1, std::round((height - columnHeight) / 2), temperature, sky, range);
    }

    dc->EndDraw();
    dc->SetTarget(nullptr);
}

// Quanto o bloco do canto cresce embaixo das legendas dos anéis (a área escura atrás estica até aqui).
float InfoPanel::Height() const {
    return bitmap ? kGapBelowRings * scale + bitmapHeight : 0;
}

// Desenha embaixo dos anéis (a transformação do monitor já está posta): x = borda esquerda do grupo de anéis, y =
// embaixo das legendas, width = largura do grupo. Clima + JSON são um bitmap refeito só quando o texto muda (no máximo
// 1x por minuto, pelo uptime); por quadro é um DrawBitmap por monitor.
void InfoPanel::Draw(ID2D1DeviceContext* screen, float x, float y, float width) {
    if (!dc) Create(screen);

    SYSTEMTIME now;
    GetLocalTime(&now);
    if (now.wSecond != second || bitmapWidth != int(width)) {
        second = now.wSecond;
        Render(now, int(width));
    }
    if (!bitmap) return;

    x = roundf(x);
    y = roundf(y + kGapBelowRings * scale);
    screen->DrawBitmap(bitmap.Get(), D2D1_RECT_F{x, y, x + bitmapWidth, y + bitmapHeight}, 1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
}
