#include "wallpaper/audio.h"
#include "wallpaper/corner_widgets.h"
#include "wallpaper/http.h"
#include "wallpaper/info_panel.h"
#include "wallpaper/palette.h"
#include "wallpaper/timing.h"
#include "wallpaper/weather.h"
#include "common/paths.h"
#include "common/registry.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <uxtheme.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

constexpr int kInkCount = int(Ink::Count);
constexpr int kFirstInk = 200;
constexpr int kJson = 100;
constexpr int kCity = 101;
constexpr int kSearch = 102;
constexpr int kResults = 103;
constexpr int kRestore = 104;
constexpr int kSave = 105;
constexpr int kGrid = 300;
constexpr int kSide = 400;
constexpr UINT kSearchDone = WM_APP + 1;
constexpr float kMargin = 20;
constexpr float kRow = 44;
constexpr float kLabelWidth = 290;
constexpr float kButtonWidth = 130;
constexpr float kButtonHeight = 32;
constexpr float kPanelWidth = 2 * kMargin + kLabelWidth + 12 + kButtonWidth;
constexpr float kPreviewWidth = 500;
constexpr float kBottom = kMargin + 14 * kRow + 8;
constexpr float kHeight = kBottom + kButtonHeight + kMargin;
constexpr COLORREF kBackground = RGB(16, 16, 18);
constexpr COLORREF kField = RGB(32, 32, 36);
constexpr COLORREF kText = RGB(222, 222, 222);

struct Place {
    std::wstring name;
    std::wstring latitude;
    std::wstring longitude;
};

struct Preview {
    ComPtr<ID3D11Device> device;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID2D1Factory1> factory;
    ComPtr<ID2D1DeviceContext> dc;
    ComPtr<ID2D1Bitmap1> target;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<ID2D1StrokeStyle> round;
    std::unique_ptr<SystemStats> stats;
    NetworkMeter network;
    NowPlayingCard card;
    Rings rings;
    InfoPanel panel;
    Equalizer equalizer;
    CornerLayout layout{};
    double lastFrame = 0;
    double lastReading = -10;
};

static HFONT font;
static HBRUSH background;
static HBRUSH field;
static float scale = 1;
static COLORREF custom[16];
static D2D1_COLOR_F pending[kInkCount];
static bool showJson = true;
static int position = 2;
static int weatherSide = 2;
static bool dirty = false;
static WPARAM searches = 0;
static Place city;
static std::vector<Place> places;
static Preview preview;

// DIPs -> pixels na escala do sistema.
static int Px(float dips) {
    return int(lroundf(dips * scale));
}

// Cor da paleta como COLORREF do GDI, e o contrário.
static COLORREF ToColorRef(D2D1_COLOR_F color) {
    return RGB(lroundf(color.r * 255), lroundf(color.g * 255), lroundf(color.b * 255));
}

static D2D1_COLOR_F ToColor(COLORREF color) {
    return {GetRValue(color) / 255.f, GetGValue(color) / 255.f, GetBValue(color) / 255.f, 1};
}

// UTF-16 <-> UTF-8 (a API de cidades fala UTF-8).
static std::string ToUtf8(std::wstring const& text) {
    std::string utf8(WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr, nullptr), 0);
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), utf8.data(), int(utf8.size()), nullptr, nullptr);
    return utf8;
}

static std::wstring FromUtf8(std::string const& text) {
    std::wstring wide(MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0), 0);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), wide.data(), int(wide.size()));
    return wide;
}

// Grava um texto ou um número em HKCU\Software\TemaBarra.
static void WriteText(const wchar_t* name, std::wstring const& value) {
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\TemaBarra", name, REG_SZ, value.c_str(), DWORD((value.size() + 1) * sizeof(wchar_t)));
}

static void WriteNumber(const wchar_t* name, DWORD value) {
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\TemaBarra", name, REG_DWORD, &value, sizeof value);
}

// Grava tudo o que está na tela: cores diferentes do padrão como "#RRGGBB" (as iguais saem do registro), o JSON
// ligado/desligado, as posições do bloco e do clima e, se mudou, a cidade (apagando o cache do clima da cidade antiga). O FundoVivo pega as cores e o
// JSON em meio segundo e o clima novo em até 10 s.
static void Save(HWND window) {
    for (int i = 0; i < kInkCount; i++) {
        COLORREF color = ToColorRef(pending[i]);
        wchar_t text[8];
        swprintf_s(text, L"#%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
        if (color == ToColorRef(kInks[i].standard)) RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\TemaBarra", kInks[i].name);
        else WriteText(kInks[i].name, text);
    }

    WriteNumber(L"Json", showJson);
    WriteNumber(L"Posicao", position);
    WriteNumber(L"PosicaoClima", weatherSide);

    if (city.latitude != SettingText(L"ClimaLatitude", L"") || city.longitude != SettingText(L"ClimaLongitude", L"")) {
        WriteText(L"ClimaLatitude", city.latitude);
        WriteText(L"ClimaLongitude", city.longitude);
        WriteText(L"ClimaCidade", city.name);
        DeleteFileW(ProjectPath(L"cache\\cache-clima.txt").c_str());
    }

    dirty = false;
    EnableWindow(GetDlgItem(window, kSave), FALSE);
}

// Alguma coisa mudou na tela: libera o Salvar e redesenha os botões e os nomes.
static void Changed(HWND window) {
    dirty = true;
    EnableWindow(GetDlgItem(window, kSave), TRUE);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

// Linha de cada opção: as cinco primeiras cores, o liga/desliga do JSON e as três cores dele (que somem com o JSON
// desligado, e o resto sobe), o clima, a posição (duas linhas), a cidade (duas linhas) e os botões.
static float RowTop(int row) {
    return kMargin + (row > 8 && !showJson ? row - 3 : row) * kRow;
}

static int InkRow(int ink) {
    return ink < 5 ? ink : ink + 1;
}

// Põe cada controle na linha dele e acerta a altura da janela (a prévia tem a altura maior e só fica cortada embaixo).
static void Arrange(HWND window) {
    const float right = kMargin + kLabelWidth + 12, bottom = RowTop(14) + 8;
    auto place = [&](int id, float x, float y, float width, float height) {
        SetWindowPos(GetDlgItem(window, id), nullptr, Px(x), Px(y), Px(width), Px(height), SWP_NOZORDER | SWP_NOACTIVATE);
    };

    for (int i = 0; i < kInkCount; i++) {
        place(kFirstInk + i, right, RowTop(InkRow(i)), kButtonWidth, kButtonHeight);
        ShowWindow(GetDlgItem(window, kFirstInk + i), i < 5 || showJson ? SW_SHOW : SW_HIDE);
    }
    place(kJson, right, RowTop(5), kButtonWidth, kButtonHeight);
    for (int i = 0; i < 3; i++) place(kSide + i, 150.f + i * 102, RowTop(9), 96, kButtonHeight);
    for (int i = 0; i < 9; i++) place(kGrid + i, right + i % 3 * 45, RowTop(10) + i / 3 * 26, 40, 22);
    place(kCity, kMargin, RowTop(12) + 4, kLabelWidth, kButtonHeight - 8);
    place(kSearch, right, RowTop(12), kButtonWidth, kButtonHeight);
    place(kResults, kMargin, RowTop(13), right - kMargin + kButtonWidth, 200);
    place(kRestore, kMargin, bottom, 150, kButtonHeight);
    place(kSave, right, bottom, kButtonWidth, kButtonHeight);

    RECT size{0, 0, Px(kPanelWidth + kPreviewWidth), Px(bottom + kButtonHeight + kMargin)};
    AdjustWindowRectEx(&size, DWORD(GetWindowLongW(window, GWL_STYLE)), FALSE, 0);
    SetWindowPos(window, nullptr, 0, 0, size.right - size.left, size.bottom - size.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Baixa o clima de um lugar numa thread à parte; o painel da prévia pega quando chegar.
static void FetchPreviewWeather(Place const& place) {
    if (place.latitude.empty()) return;
    std::thread([place] { FetchWeather(place.latitude, place.longitude); }).detach();
}

// Texto de "chave": valor (string ou número) num objeto JSON simples, sem aspas escapadas.
static std::wstring JsonValue(std::string const& object, const char* key) {
    std::string pattern = std::string("\"") + key + "\":";
    size_t p = object.find(pattern);
    if (p == std::string::npos) return L"";

    p += pattern.size();
    size_t end = object[p] == '"' ? object.find('"', ++p) : object.find_first_of(",}", p);
    return FromUtf8(object.substr(p, end - p));
}

// Lugares da resposta do geocoding do Open-Meteo: nome, estado e país, com latitude e longitude.
static std::vector<Place> ParsePlaces(std::string const& json) {
    std::vector<Place> found;
    for (size_t p = json.find("{\"id\":"); p != std::string::npos;) {
        size_t next = json.find("{\"id\":", p + 1);
        std::string object = json.substr(p, next - p);
        Place place{JsonValue(object, "name"), JsonValue(object, "latitude"), JsonValue(object, "longitude")};
        for (const char* key : {"admin1", "country"})
            if (auto part = JsonValue(object, key); !part.empty() && part != place.name) place.name += L", " + part;
        if (!place.latitude.empty()) found.push_back(place);
        p = next;
    }
    return found;
}

// Procura a cidade digitada (até 5 lugares com esse nome) numa thread à parte; a resposta volta em kSearchDone.
static void Search(HWND window) {
    wchar_t text[128];
    GetDlgItemTextW(window, kCity, text, 128);
    std::wstring query;
    for (unsigned char c : ToUtf8(text)) {
        wchar_t escaped[4];
        swprintf_s(escaped, L"%%%02X", c);
        query += isalnum(c) ? std::wstring(1, wchar_t(c)) : escaped;
    }
    if (query.empty()) return;

    SetDlgItemTextW(window, kSearch, L"Buscando...");
    std::thread([window, query, id = ++searches] {
        std::string json = HttpsRequest(L"geocoding-api.open-meteo.com", L"/v1/search?count=5&language=pt&format=json&name=" + query);
        PostMessageW(window, kSearchDone, id, LPARAM(new std::vector<Place>(ParsePlaces(json))));
    }).detach();
}

// Lista de lugares na caixa de seleção (o primeiro já escolhido); sem nenhum, só o aviso.
static void ShowPlaces(HWND window, std::vector<Place> const& found) {
    HWND list = GetDlgItem(window, kResults);
    places = found;
    SendMessageW(list, CB_RESETCONTENT, 0, 0);
    for (auto const& place : places) SendMessageW(list, CB_ADDSTRING, 0, LPARAM(place.name.c_str()));
    if (places.empty()) SendMessageW(list, CB_ADDSTRING, 0, LPARAM(L"Nenhuma cidade com esse nome"));
    SendMessageW(list, CB_SETCURSEL, 0, 0);
}

// Seletor de cor do Windows, começando na cor da tela; as cores padrão ficam nos quadradinhos personalizados.
static void Pick(HWND window, int ink) {
    CHOOSECOLORW dialog{sizeof dialog, window};
    dialog.rgbResult = ToColorRef(pending[ink]);
    dialog.lpCustColors = custom;
    dialog.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
    if (!ChooseColorW(&dialog) || dialog.rgbResult == ToColorRef(pending[ink])) return;

    pending[ink] = ToColor(dialog.rgbResult);
    Changed(window);
}

// Botões desenhados à mão (fundo escuro): o de cada cor é a própria cor com o "#RRGGBB" em preto ou branco, conforme o
// que lê melhor; o do JSON, a posição escolhida na grade 3x3 (sem texto, é uma tela em miniatura) e o lado do clima
// ficam claros; o Salvar é claro e apagado sem mudança; os outros, cinza. Borda clara com o foco do teclado;
// apertado, fica mais escuro.
static void DrawButton(DRAWITEMSTRUCT const& item) {
    int id = item.CtlID;
    bool ink = id >= kFirstInk && id < kFirstInk + kInkCount, grid = id >= kGrid && id < kGrid + 9, side = id >= kSide && id < kSide + 3;
    bool on = ink || id == kSave || (id == kJson && showJson) || (grid && id - kGrid == position) || (side && id - kSide == weatherSide);
    bool disabled = item.itemState & ODS_DISABLED;
    COLORREF fill = ink ? ToColorRef(pending[id - kFirstInk]) : on && !disabled ? RGB(232, 232, 232) : RGB(44, 44, 48);
    if (item.itemState & ODS_SELECTED) fill = RGB(GetRValue(fill) * 3 / 4, GetGValue(fill) * 3 / 4, GetBValue(fill) * 3 / 4);

    wchar_t text[64];
    if (ink) swprintf_s(text, L"#%02X%02X%02X", GetRValue(fill), GetGValue(fill), GetBValue(fill));
    else if (id == kJson) wcscpy_s(text, showJson ? L"Aparecendo" : L"Escondido");
    else if (grid) text[0] = 0;
    else GetWindowTextW(item.hwndItem, text, 64);
    bool light = GetRValue(fill) * 0.299 + GetGValue(fill) * 0.587 + GetBValue(fill) * 0.114 > 140;

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, Px(1), item.itemState & ODS_FOCUS ? RGB(230, 230, 230) : RGB(70, 70, 76));
    RECT box = item.rcItem;
    FillRect(item.hDC, &box, background);
    HGDIOBJ oldBrush = SelectObject(item.hDC, brush), oldPen = SelectObject(item.hDC, pen), oldFont = SelectObject(item.hDC, font);
    RoundRect(item.hDC, box.left, box.top, box.right, box.bottom, Px(grid ? 6.f : 10.f), Px(grid ? 6.f : 10.f));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, disabled ? RGB(110, 110, 116) : light ? RGB(0, 0, 0) : kText);
    DrawTextW(item.hDC, text, -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(item.hDC, oldBrush);
    SelectObject(item.hDC, oldPen);
    SelectObject(item.hDC, oldFont);
    DeleteObject(brush);
    DeleteObject(pen);
}

// Nome de cada opção à esquerda do botão dela (a da posição fica no meio das duas linhas da grade).
static void PaintLabels(HWND window) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kText);
    auto label = [&](const wchar_t* text, float top) {
        RECT box{Px(kMargin), Px(top), Px(kMargin + kLabelWidth), Px(top + kButtonHeight)};
        DrawTextW(dc, text, -1, &box, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    };

    for (int i = 0; i < kInkCount; i++)
        if (i < 5 || showJson) label(kInks[i].label, RowTop(InkRow(i)));
    label(L"JSON embaixo dos anéis", RowTop(5));
    label(L"Clima", RowTop(9));
    label(L"Posição na tela", RowTop(10) + kRow / 2);

    SelectObject(dc, oldFont);
    EndPaint(window, &paint);
}

// Direct3D + Direct2D na janela da prévia (como no FundoVivo) e os mesmos widgets dele: cartão da música, anéis e o
// painel do JSON/clima, num bloco de 380 DIPs com a margem do canto.
static bool CreatePreview(HWND window, int width, int height) {
    auto& p = preview;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                 &p.device, nullptr, nullptr)))
        return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> dxgiFactory;
    p.device.As(&dxgiDevice);
    dxgiDevice->GetAdapter(&adapter);
    adapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
    DXGI_SWAP_CHAIN_DESC1 desc{UINT(width), UINT(height), DXGI_FORMAT_B8G8R8A8_UNORM, FALSE, {1, 0}, DXGI_USAGE_RENDER_TARGET_OUTPUT, 2,
                               DXGI_SCALING_STRETCH, DXGI_SWAP_EFFECT_FLIP_DISCARD};
    if (FAILED(dxgiFactory->CreateSwapChainForHwnd(p.device.Get(), window, &desc, nullptr, nullptr, &p.swapChain))) return false;

    ComPtr<ID2D1Device> d2dDevice;
    ComPtr<IDXGISurface> surface;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, p.factory.GetAddressOf());
    p.factory->CreateDevice(dxgiDevice.Get(), &d2dDevice);
    d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &p.dc);
    p.dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    p.swapChain->GetBuffer(0, IID_PPV_ARGS(&surface));
    D2D1_BITMAP_PROPERTIES1 properties{{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE}, 96, 96,
                                       D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW};
    p.dc->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &p.target);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)p.dwrite.GetAddressOf());
    p.dc->CreateSolidColorBrush({1, 1, 1, 1}, &p.brush);
    p.factory->CreateStrokeStyle({D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND, 10, D2D1_DASH_STYLE_SOLID, 0},
                                 nullptr, 0, &p.round);

    DXGI_ADAPTER_DESC adapterDesc{};
    adapter->GetDesc(&adapterDesc);
    p.stats = std::make_unique<SystemStats>(adapterDesc.AdapterLuid);
    p.card.Create(p.dwrite.Get(), scale);
    p.rings.Create(p.factory.Get(), p.dwrite.Get(), scale);
    float cardLeft = float(Px((kPreviewWidth - 380) / 2));
    p.layout = {cardLeft + 380 * scale, 30 * scale, cardLeft, cardLeft + CoverSize() + 16 * scale, 36 * scale, scale};
    p.lastFrame = Now();
    return true;
}

// Um quadro da prévia com o que está na tela (ainda não salvo): uma música de exemplo tocando, o equalizador dançando
// sozinho, os anéis com os números de verdade e o JSON/clima do jeito que o FundoVivo desenha.
static void DrawPreview() {
    auto& p = preview;
    if (!p.dc) return;

    double now = Now();
    float dt = std::min(float(now - p.lastFrame), 0.1f);
    p.lastFrame = now;
    SetPalette(pending);
    p.panel.showJson = showJson;
    p.panel.weatherSide = weatherSide;

    NowPlaying song{L"Música", L"Artista", true};
    song.position = 83;
    song.duration = 214;
    song.readAt = now;
    p.equalizer.Simulate(now, dt);
    p.card.Update(p.dc.Get(), p.dwrite.Get(), song, NextTrack{}, p.layout, dt);
    if (now - p.lastReading > 1.5) {
        p.lastReading = now;
        p.stats->Read();
        double networkPercent = p.network.Read();
        p.rings.Update(*p.stats, p.network, networkPercent);
    }

    auto const& l = p.layout;
    float a = p.card.alpha * p.card.alpha * (3 - 2 * p.card.alpha), ringsY = roundf(l.top + 118 * l.scale + l.ringRadius + 2 * l.scale);
    p.dc->SetTarget(p.target.Get());
    p.dc->BeginDraw();
    p.dc->Clear({0, 0, 0, 1});
    p.dc->SetTransform(D2D1::Matrix3x2F::Identity());
    p.card.Draw(p.dc.Get(), p.brush.Get(), l, p.equalizer, a, -1, -1, false, false);
    p.rings.Draw(p.dc.Get(), p.brush.Get(), p.round.Get(), D2D1::Matrix3x2F::Identity(), l, ringsY);
    p.panel.Draw(p.dc.Get(), l.cardLeft, ringsY + l.ringRadius + 22 * l.scale, l.right - l.cardLeft, false);
    p.dc->EndDraw();
    p.dc->SetTarget(nullptr);
    p.swapChain->Present(1, 0);
}

// Pergunta se salva antes de fechar com mudança pendente; false = o usuário desistiu de fechar.
static bool ConfirmClose(HWND window) {
    if (!dirty) return true;

    int answer = MessageBoxW(window, L"Salvar as mudanças antes de fechar?", L"Wallget", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (answer == IDYES) Save(window);
    return answer != IDCANCEL;
}

// Cliques: cor abre o seletor, JSON alterna, grade e lados escolhem as posições, Buscar (ou Enter na caixa da cidade)
// procura, a lista escolhe a cidade, Restaurar volta cores, JSON e posições ao padrão (sem salvar) e Salvar grava.
static void Command(HWND window, int id, int code) {
    if (id >= kFirstInk && id < kFirstInk + kInkCount) {
        Pick(window, id - kFirstInk);
    } else if (id == kJson) {
        showJson = !showJson;
        Arrange(window);
        Changed(window);
    } else if (id >= kGrid && id < kGrid + 9) {
        position = id - kGrid;
        Changed(window);
    } else if (id >= kSide && id < kSide + 3) {
        weatherSide = id - kSide;
        Changed(window);
    } else if (id == kSearch || (id == IDOK && GetFocus() == GetDlgItem(window, kCity))) {
        Search(window);
    } else if (id == kResults && code == CBN_SELCHANGE) {
        size_t chosen = SendMessageW(GetDlgItem(window, kResults), CB_GETCURSEL, 0, 0);
        if (chosen >= places.size() || places[chosen].latitude == city.latitude && places[chosen].longitude == city.longitude) return;
        city = places[chosen];
        FetchPreviewWeather(city);
        Changed(window);
    } else if (id == kRestore) {
        for (int i = 0; i < kInkCount; i++) pending[i] = kInks[i].standard;
        showJson = true;
        position = weatherSide = 2;
        Arrange(window);
        Changed(window);
    } else if (id == kSave) {
        Save(window);
    }
}

// Janela principal: opções à esquerda, prévia à direita (redesenhada a ~30 fps por um timer).
static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_DRAWITEM:
        DrawButton(*(DRAWITEMSTRUCT*)lParam);
        return TRUE;
    case WM_PAINT:
        PaintLabels(window);
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor(HDC(wParam), kText);
        SetBkColor(HDC(wParam), kField);
        return LRESULT(field);
    case WM_COMMAND:
        Command(window, LOWORD(wParam), HIWORD(wParam));
        return 0;
    case kSearchDone: {
        std::unique_ptr<std::vector<Place>> found((std::vector<Place>*)lParam);
        if (wParam != searches) return 0;  // resposta de uma busca anterior, que chegou depois
        SetDlgItemTextW(window, kSearch, L"Buscar");
        ShowPlaces(window, *found);
        Command(window, kResults, CBN_SELCHANGE);
        return 0;
    }
    case WM_TIMER:
        if (!IsIconic(window)) DrawPreview();
        return 0;
    case WM_CLOSE:
        if (ConfirmClose(window)) DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

// Um controle filho com a fonte da janela (o lugar dele vem do Arrange).
static HWND Child(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style, int id) {
    HWND child = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, parent, HMENU(INT_PTR(id)),
                                 GetModuleHandleW(nullptr), nullptr);
    SendMessageW(child, WM_SETFONT, WPARAM(font), FALSE);
    return child;
}

// Opções da tela começam no que está salvo: cores, JSON, posições e a cidade (sem o nome salvo, aparece como "Lugar atual").
static void LoadSettings() {
    for (int i = 0; i < kInkCount; i++) {
        pending[i] = ReadInk(Ink(i));
        custom[i] = ToColorRef(kInks[i].standard);
    }
    showJson = Enabled(L"Json");
    position = int(Setting(L"Posicao", 2) % 9);
    weatherSide = int(Setting(L"PosicaoClima", 2) % 3);
    city = {SettingText(L"ClimaCidade", L"Lugar atual"), SettingText(L"ClimaLatitude", L""), SettingText(L"ClimaLongitude", L"")};
    if (city.latitude.empty()) city.name = L"Sem clima (escolha uma cidade)";
}

// FundoConfig.exe (Wallget no instalador): configura os widgets do papel de parede (cores, JSON, posições e cidade do
// clima) com uma prévia ao lado; nada vai para o papel de parede antes do Salvar.
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    scale = GetDpiForSystem() / 96.f;
    font = CreateFontW(-Px(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Variable Text");
    background = CreateSolidBrush(kBackground);
    field = CreateSolidBrush(kField);
    LoadSettings();

    WNDCLASSW windowClass{0, WindowProc, 0, 0, instance, LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION),
                          LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW), background, nullptr, L"FundoConfig"};
    WNDCLASSW previewClass{0, DefWindowProcW, 0, 0, instance, nullptr, nullptr, (HBRUSH)GetStockObject(BLACK_BRUSH), nullptr, L"FundoPrevia"};
    RegisterClassW(&windowClass);
    RegisterClassW(&previewClass);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT size{0, 0, Px(kPanelWidth + kPreviewWidth), Px(kHeight)};
    AdjustWindowRectEx(&size, style, FALSE, 0);
    HWND window = CreateWindowExW(0, L"FundoConfig", L"Wallget", style, CW_USEDEFAULT, CW_USEDEFAULT, size.right - size.left,
                                  size.bottom - size.top, nullptr, nullptr, instance, nullptr);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);

    for (int i = 0; i < kInkCount; i++) {
        if (i == 5) Child(window, L"BUTTON", L"JSON embaixo dos anéis", WS_TABSTOP | BS_OWNERDRAW, kJson);
        Child(window, L"BUTTON", kInks[i].label, WS_TABSTOP | BS_OWNERDRAW, kFirstInk + i);
    }
    const wchar_t* sides[3] = {L"Esquerda", L"Meio", L"Direita"};
    for (int i = 0; i < 3; i++) Child(window, L"BUTTON", sides[i], WS_TABSTOP | BS_OWNERDRAW, kSide + i);
    const wchar_t* cells[9] = {L"Em cima, à esquerda", L"Em cima, no meio", L"Em cima, à direita", L"No meio, à esquerda", L"No centro",
                               L"No meio, à direita", L"Embaixo, à esquerda", L"Embaixo, no meio", L"Embaixo, à direita"};
    for (int i = 0; i < 9; i++) Child(window, L"BUTTON", cells[i], WS_TABSTOP | BS_OWNERDRAW, kGrid + i);
    HWND cityBox = Child(window, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, kCity);
    SendMessageW(cityBox, EM_SETCUEBANNER, TRUE, LPARAM(L"Cidade do clima (ex.: Campinas)"));
    Child(window, L"BUTTON", L"Buscar", WS_TABSTOP | BS_OWNERDRAW, kSearch);
    HWND list = Child(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, kResults);
    Child(window, L"BUTTON", L"Restaurar padrão", WS_TABSTOP | BS_OWNERDRAW, kRestore);
    EnableWindow(Child(window, L"BUTTON", L"Salvar", WS_TABSTOP | BS_OWNERDRAW, kSave), FALSE);
    for (HWND control : {cityBox, list}) SetWindowTheme(control, L"DarkMode_CFD", nullptr);
    ShowPlaces(window, {city});
    if (city.latitude.empty()) places.clear();
    Arrange(window);

    HWND previewWindow = Child(window, L"FundoPrevia", L"", 0, 0);
    SetWindowPos(previewWindow, nullptr, Px(kPanelWidth), 0, Px(kPreviewWidth), Px(kHeight), SWP_NOZORDER | SWP_NOACTIVATE);
    if (CreatePreview(previewWindow, Px(kPreviewWidth), Px(kHeight))) SetTimer(window, 1, 33, nullptr);
    FetchPreviewWeather(city);

    ShowWindow(window, show);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(window, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
