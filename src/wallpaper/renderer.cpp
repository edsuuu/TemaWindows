#include "wallpaper/renderer.h"
#include "wallpaper/corner_widgets.h"
#include "wallpaper/desktop.h"
#include "wallpaper/golden_theme.h"
#include "wallpaper/info_panel.h"
#include "wallpaper/media_buttons.h"
#include "wallpaper/morph_theme.h"
#include "wallpaper/nbhd_theme.h"
#include "wallpaper/timing.h"
#include "wallpaper/weather.h"
#include "common/paths.h"
#include "common/registry.h"

#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <algorithm>
#include <cmath>
#include <vector>

struct Graphics {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> dxgiFactory;
    ComPtr<ID2D1Factory1> d2dFactory;
    ComPtr<ID2D1DeviceContext> dc;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<ID2D1StrokeStyle> round;
    float scale = 1;
};

struct Surface {
    HWND window = nullptr;
    RECT size{};
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID2D1Bitmap1> bitmap;
};

struct Monitors {
    std::vector<RECT> areas;
    std::vector<D2D1::Matrix3x2F> corners;
    float primaryWidth = 0;
    float primaryHeight = 0;
    CornerLayout layout{};
};

// Tema escolhido em HKCU\Software\TemaBarra\FundoTema: "golden" = fundo.png ondulando ('g'); "loop" = alga <-> logo do
// Windows ('l'); qualquer outra coisa (ou nada) = "nbhd", a tela de start retrô ('n').
static char ReadTheme() {
    std::wstring theme = SettingText(L"FundoTema", L"nbhd");
    return !_wcsicmp(theme.c_str(), L"golden") ? 'g' : !_wcsicmp(theme.c_str(), L"loop") ? 'l' : 'n';
}

// Direct3D 11 com suporte a BGRA (para o Direct2D desenhar no mesmo quadro) e a fábrica DXGI do adaptador.
static bool CreateDevice(Graphics& g) {
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                 &g.device, nullptr, &g.context)))
        return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    g.device.As(&dxgiDevice);
    dxgiDevice->GetAdapter(&g.adapter);
    g.adapter->GetParent(IID_PPV_ARGS(&g.dxgiFactory));
    return true;
}

// Direct2D/DirectWrite por cima do mesmo quadro: contexto de desenho, pincel e traço de pontas redondas.
static void CreateDrawing(Graphics& g) {
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<ID2D1Device> d2dDevice;
    g.device.As(&dxgiDevice);
    g.scale = DpiScale();

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g.d2dFactory.GetAddressOf());
    g.d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice);
    d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g.dc);
    g.dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)g.dwrite.GetAddressOf());

    g.dc->CreateSolidColorBrush({1, 1, 1, 1}, &g.brush);
    D2D1_STROKE_STYLE_PROPERTIES roundCaps{D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND, 10,
                                           D2D1_DASH_STYLE_SOLID, 0};
    g.d2dFactory->CreateStrokeStyle(roundCaps, nullptr, 0, &g.round);
}

// LUID do adaptador de vídeo (para ler o uso da GPU).
static LUID AdapterLuid(Graphics const& g) {
    DXGI_ADAPTER_DESC desc{};
    g.adapter->GetDesc(&desc);
    return desc.AdapterLuid;
}

// Janela filha do WorkerW, do tamanho dele (todos os monitores), com a cadeia de troca e o alvo do Direct2D.
static bool CreateSurface(Graphics const& g, HINSTANCE instance, HWND parent, Surface& surface) {
    GetClientRect(parent, &surface.size);
    surface.window = CreateWindowExW(0, L"FundoVivo", L"", WS_CHILD | WS_VISIBLE | WS_DISABLED, 0, 0, surface.size.right, surface.size.bottom,
                                     parent, nullptr, instance, nullptr);

    DXGI_SWAP_CHAIN_DESC1 desc{(UINT)surface.size.right, (UINT)surface.size.bottom, DXGI_FORMAT_B8G8R8A8_UNORM, FALSE, {1, 0},
                               DXGI_USAGE_RENDER_TARGET_OUTPUT, 2, DXGI_SCALING_STRETCH, DXGI_SWAP_EFFECT_FLIP_DISCARD};
    if (!surface.window ||
        FAILED(g.dxgiFactory->CreateSwapChainForHwnd(g.device.Get(), surface.window, &desc, nullptr, nullptr, &surface.swapChain))) {
        if (surface.window) DestroyWindow(surface.window);
        return false;
    }

    ComPtr<ID3D11Texture2D> back;
    ComPtr<IDXGISurface> dxgiSurface;
    surface.swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    g.device->CreateRenderTargetView(back.Get(), nullptr, &surface.target);
    back.As(&dxgiSurface);

    D2D1_BITMAP_PROPERTIES1 properties{{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE}, 96, 96,
                                       D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW};
    g.dc->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), &properties, &surface.bitmap);
    return true;
}

// Monitores em coordenadas da nossa janela. O bloco do canto (música + indicadores) fica no canto de cima à direita da
// área útil de cada monitor: é desenhado no lugar do principal e deslocado para os outros. O cartão tem largura fixa,
// com a capa à esquerda e o texto e o equalizador colados nela até a borda direita.
static Monitors ReadMonitors(HWND parent, float scale) {
    std::vector<MONITORINFO> infos;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM p) -> BOOL {
        MONITORINFO info{sizeof info};
        GetMonitorInfoW(monitor, &info);
        ((std::vector<MONITORINFO>*)p)->push_back(info);
        return TRUE;
    }, (LPARAM)&infos);

    MONITORINFO primary{sizeof primary};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &primary);

    Monitors monitors;
    for (auto& info : infos) {
        MapWindowPoints(HWND_DESKTOP, parent, (POINT*)&info.rcMonitor, 2);
        monitors.areas.push_back(info.rcMonitor);
        monitors.corners.push_back(
            D2D1::Matrix3x2F::Translation(float(info.rcWork.right - primary.rcWork.right), float(info.rcWork.top - primary.rcWork.top)));
    }
    MapWindowPoints(HWND_DESKTOP, parent, (POINT*)&primary.rcWork, 2);

    monitors.primaryWidth = float(primary.rcMonitor.right - primary.rcMonitor.left);
    monitors.primaryHeight = float(primary.rcMonitor.bottom - primary.rcMonitor.top);
    float right = primary.rcWork.right - 44 * scale, cardLeft = roundf(right - 380 * scale);
    monitors.layout = {right, primary.rcWork.top + 36 * scale, cardLeft, cardLeft + CoverSize() + 16 * scale, 36 * scale, scale};
    return monitors;
}

struct Block {
    float left;
    float top;
    float right;
    float bottom;
    float ringsY;
};

// Estado que atravessa as sessões (o Explorer reiniciando recria só a janela): temas, som, widgets e tempos.
class Wallpaper {
public:
    Wallpaper(Graphics& graphics, GoldenTheme& golden) : g(graphics), golden(golden), stats(AdapterLuid(graphics)) {
        card.Create(g.dwrite.Get(), g.scale);
        rings.Create(g.d2dFactory.Get(), g.dwrite.Get(), g.scale);
        timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        start = previousFrame = Now();
    }

    void RunSession(HWND parent, Surface const& surface);

private:
    Graphics& g;
    GoldenTheme& golden;
    MorphTheme morph;
    NbhdTheme nbhd;
    Equalizer equalizer;
    NowPlayingCard card;
    Rings rings;
    InfoPanel panel;
    SystemStats stats;
    NetworkMeter network;
    ComPtr<IAudioClient> audioClient;
    ComPtr<IAudioCaptureClient> capture;
    HANDLE timer;
    ULONGLONG nextCaptureAttempt = 0;
    double start;
    double previousFrame;
    double lastReading = -10;
    float presence = 0;
    bool morphLoaded = false;
    char theme = 'n';

    void UpdateCapture(NowPlaying const& song, bool hidden, ULONGLONG now);
    bool CheckState(HWND parent, Surface const& surface, NowPlaying const& song, ULONGLONG now, bool& hidden);
    void UpdateWidgets(NowPlaying const& song, Monitors const& monitors, double now);
    Block LayoutBlock(Monitors const& monitors) const;
    void DrawBackground(Surface const& surface, Monitors const& monitors, Block const& block, double now);
    void DrawOverlay(Surface const& surface, Monitors const& monitors, Block const& block, double now);
    void RenderFrame(Surface const& surface, Monitors const& monitors, NowPlaying const& song);
    void UpdateButtons(HWND parent, Monitors const& monitors, bool hidden);
    void WaitNextFrame(double frameStart);
};

// Captura o som só enquanto o Spotify toca e alguém vê o fundo; se falhar, tenta de novo em 10 s.
void Wallpaper::UpdateCapture(NowPlaying const& song, bool hidden, ULONGLONG now) {
    if (song.playing && !hidden && !capture && now >= nextCaptureAttempt) {
        capture = StartLoopbackCapture(audioClient);
        if (!capture) {
            audioClient = nullptr;
            nextCaptureAttempt = now + 10000;
        }
    } else if ((!song.playing || hidden) && audioClient) {
        audioClient->Stop();
        capture = nullptr;
        audioClient = nullptr;
    }
}

// A cada meio segundo: fundo visível?, cor da seleção, tema, captura de som e tamanho da área de trabalho. Resolução ou
// monitores mudaram (ou o Explorer levou o WorkerW junto): destrói a janela e devolve false para montar tudo de novo.
bool Wallpaper::CheckState(HWND parent, Surface const& surface, NowPlaying const& song, ULONGLONG now, bool& hidden) {
    hidden = ComputeWallpaperHidden();
    SetWallpaperHidden(hidden);
    RestoreSelectionColors();
    theme = ReadTheme();
    UpdateCapture(song, hidden, now);

    RECT current;
    bool sameSize = IsWindow(parent) && GetClientRect(parent, &current) && current.right == surface.size.right &&
                    current.bottom == surface.size.bottom;
    if (sameSize) return true;

    DestroyWindow(surface.window);
    return false;
}

// Equalizador, cartão da música, a subida/descida do bloco e, a cada 1,5 s (só com o fundo visível), os anéis e a rede;
// o clima começa junto da primeira leitura.
void Wallpaper::UpdateWidgets(NowPlaying const& song, Monitors const& monitors, double now) {
    float dt = std::min(float(now - previousFrame), 0.1f);
    previousFrame = now;

    equalizer.Read(capture.Get());
    if (card.alpha > 0 || capture) equalizer.Update(capture != nullptr, dt);
    card.Update(g.dc.Get(), g.dwrite.Get(), song, monitors.layout, dt);
    presence = std::clamp(presence + (card.showing ? 2 : -2) * dt, 0.f, 1.f);

    if (now - lastReading <= 1.5) return;
    lastReading = now;
    stats.Read();
    StartWeatherService();
    double networkPercent = network.Read();
    rings.Update(stats, network, networkPercent);
}

// Retângulo do bloco do canto (cartão + anéis + painel; sem música, só anéis e painel, que sobem para o lugar do
// cartão), para a área escura atrás dele.
Block Wallpaper::LayoutBlock(Monitors const& monitors) const {
    auto const& l = monitors.layout;
    const float s = l.scale, r = l.ringRadius;
    float a = card.alpha * card.alpha * (3 - 2 * card.alpha);
    float ringsY = roundf(l.top + presence * presence * (3 - 2 * presence) * 118 * s + r + 2 * s);
    float center = (l.cardLeft + l.right) / 2, halfWidth = 4 * r + 30 * s;

    return {center - halfWidth + (l.cardLeft - center + halfWidth) * a, ringsY - r + (l.top - ringsY + r) * a,
            center + halfWidth + (l.right - center - halfWidth) * a, ringsY + r + 22 * s + panel.Height(), ringsY};
}

// Direct3D: fundo preto e, em cada monitor, a imagem inteira ondulando ("golden") ou as faixas do morph ("loop"). Cada
// tema só mantém os recursos dele carregados.
void Wallpaper::DrawBackground(Surface const& surface, Monitors const& monitors, Block const& block, double now) {
    float black[4] = {0, 0, 0, 1};
    auto* context = g.context.Get();
    context->ClearRenderTargetView(surface.target.Get(), black);
    context->OMSetRenderTargets(1, surface.target.GetAddressOf(), nullptr);

    if (theme == 'g' && !golden.Loaded()) golden.Load(g.device.Get(), context, ProjectPath(L"assets\\fundo.png"));
    if (theme != 'g') golden.Unload();
    if (theme != 'n') nbhd.Release();
    if (theme == 'l' && !morphLoaded) morphLoaded = morph.Load(g.device.Get(), ProjectPath(L"assets\\fundo-morph.bin"));

    for (size_t i = 0; i < monitors.areas.size(); i++) {
        RECT const& area = monitors.areas[i];
        auto const& corner = monitors.corners[i];
        const float shade[4] = {block.left + corner._31, block.top + corner._32, block.right + corner._31, block.bottom + corner._32};

        if (theme == 'l' && morphLoaded) {
            D3D11_VIEWPORT viewport{0, 0, float(surface.size.right), float(surface.size.bottom), 0, 1};
            context->RSSetViewports(1, &viewport);
            morph.Update(float(fmod(now - start, double(MorphTheme::kLoopSeconds))), float(area.right - area.left), float(area.bottom - area.top),
                         float(area.left), float(area.top));
            morph.Draw(context, float(surface.size.right), float(surface.size.bottom), shade);
        } else if (theme == 'g' && golden.Loaded()) {
            golden.Draw(context, area, float(fmod(now - start, 84000.0)), shade);
        }
    }
}

// Direct2D por cima: a arte do tema "nbhd" e, em cada monitor, o bloco do canto (cartão da música, anéis e painel). O
// tempo volta a 0 a cada ~23 h para o float não perder precisão (dá um pulinho uma vez por dia).
void Wallpaper::DrawOverlay(Surface const& surface, Monitors const& monitors, Block const& block, double now) {
    auto* dc = g.dc.Get();
    auto const& l = monitors.layout;
    float a = card.alpha * card.alpha * (3 - 2 * card.alpha);

    if (theme == 'n' && !nbhd.Built()) nbhd.Build(g.d2dFactory.Get(), dc, g.brush.Get());
    dc->SetTarget(surface.bitmap.Get());
    dc->BeginDraw();

    if (theme == 'n' && nbhd.Built()) {
        float bass = (equalizer.level[0] + equalizer.level[1] + equalizer.level[2] + equalizer.level[3]) / 4;
        nbhd.Draw(dc, g.brush.Get(), g.round.Get(), monitors.areas, float(fmod(now - start, 84000.0)), capture != nullptr, bass);
    }

    for (int i = 0; i < int(monitors.corners.size()); i++) {
        auto const& corner = monitors.corners[i];
        dc->SetTransform(corner);
        if (a > 0) card.Draw(dc, g.brush.Get(), l, equalizer, a, HoveredMediaButton(i), PressedMediaButton(i));
        rings.Draw(dc, g.brush.Get(), g.round.Get(), corner, l, block.ringsY);
        panel.Draw(dc, l.cardLeft, block.ringsY + l.ringRadius + 22 * l.scale, l.right - l.cardLeft);
    }

    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    dc->EndDraw();
    dc->SetTarget(nullptr);
}

// Janelas que pegam o clique dos botões do cartão, uma por monitor, sobre os três botões (só com o cartão aparecendo e
// o fundo visível).
void Wallpaper::UpdateButtons(HWND parent, Monitors const& monitors, bool hidden) {
    D2D1_RECT_F first = MediaButtonRect(monitors.layout, 0), last = MediaButtonRect(monitors.layout, 2);
    std::vector<RECT> rects;

    for (auto const& corner : monitors.corners) {
        RECT r{LONG(first.left + corner._31), LONG(first.top + corner._32), LONG(last.right + corner._31), LONG(last.bottom + corner._32)};
        MapWindowPoints(parent, HWND_DESKTOP, (POINT*)&r, 2);
        rects.push_back(r);
    }
    PlaceMediaButtons(rects, card.alpha > 0.5f && !hidden);
}

// Um quadro inteiro.
void Wallpaper::RenderFrame(Surface const& surface, Monitors const& monitors, NowPlaying const& song) {
    double now = Now();
    UpdateWidgets(song, monitors, now);

    Block block = LayoutBlock(monitors);
    DrawBackground(surface, monitors, block, now);
    DrawOverlay(surface, monitors, block, now);
    surface.swapChain->Present(1, 0);
    WaitNextFrame(now);
}

// 60 fps enquanto o equalizador anda; senão ~30 (o movimento é lento, mais seria desperdício). O monitor é de 239 Hz,
// então quem dita o ritmo é este timer de alta resolução (o Sleep só tem resolução de ~16 ms).
void Wallpaper::WaitNextFrame(double frameStart) {
    double frame = capture ? 1 / 60.0 : 1 / 30.0;
    LARGE_INTEGER due{.QuadPart = -LONGLONG(1e7 * std::max(0.001, frame - (Now() - frameStart)))};

    SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
    WaitForSingleObject(timer, 100);
}

// Uma sessão da janela no WorkerW: desenha ao menos um quadro (mesmo se já abrir pausado) e depois só com alguém vendo
// o fundo; pausado, fica em 0% de GPU.
void Wallpaper::RunSession(HWND parent, Surface const& surface) {
    Monitors monitors = ReadMonitors(parent, g.scale);
    nbhd.Reset(monitors.primaryWidth, monitors.primaryHeight);
    bool hidden = false, drawn = false;
    ULONGLONG lastCheck = 0;

    while (IsWindow(surface.window)) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);

        ULONGLONG now = GetTickCount64();
        NowPlaying song = CurrentMedia();
        if (now - lastCheck > 500) {
            lastCheck = now;
            if (!CheckState(parent, surface, song, now, hidden)) break;
            UpdateButtons(parent, monitors, hidden);
        }

        if (hidden && drawn) {
            Sleep(200);
            continue;
        }

        drawn = true;
        RenderFrame(surface, monitors, song);
    }
}

// Solta tudo o que prendia a janela antiga antes de montar a próxima.
static void ReleaseSession(Graphics& g) {
    g.dc->SetTarget(nullptr);
    g.context->OMSetRenderTargets(0, nullptr, nullptr);
    g.context->ClearState();
    g.context->Flush();
}

// Laço do papel de parede: uma janela atrás dos ícones da área de trabalho (filha do WorkerW) que desenha o tema e o
// bloco do canto em todos os monitores. Se o Explorer reiniciar (o WorkerW some e leva a janela junto) ou a tela mudar,
// monta tudo de novo.
int RunWallpaper(HINSTANCE instance) {
    Graphics graphics;
    GoldenTheme golden;
    if (!CreateDevice(graphics) || !golden.Create(graphics.device.Get())) return 1;
    CreateDrawing(graphics);

    Wallpaper wallpaper(graphics, golden);
    WNDCLASSW windowClass{0, DefWindowProcW, 0, 0, instance, nullptr, nullptr, (HBRUSH)GetStockObject(BLACK_BRUSH), nullptr, L"FundoVivo"};
    RegisterClassW(&windowClass);

    for (;;) {
        HWND parent;
        while (!(parent = FindWallpaperParent())) Sleep(1000);

        Surface surface;
        if (!CreateSurface(graphics, instance, parent, surface)) {
            Sleep(3000);
            continue;
        }

        wallpaper.RunSession(parent, surface);
        ReleaseSession(graphics);
        Sleep(2000);
    }
}
