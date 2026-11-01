#include "wallpaper/renderer.h"
#include "wallpaper/corner_widgets.h"
#include "wallpaper/desktop.h"
#include "wallpaper/golden_theme.h"
#include "wallpaper/info_panel.h"
#include "wallpaper/click_zones.h"
#include "wallpaper/morph_theme.h"
#include "wallpaper/nbhd_theme.h"
#include "wallpaper/palette.h"
#include "wallpaper/timing.h"
#include "wallpaper/video_theme.h"
#include "wallpaper/weather.h"
#include "wallpaper/widget_options.h"
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
    std::vector<RECT> works;
    std::vector<std::wstring> devices;
    std::vector<D2D1::Matrix3x2F> corners;
    std::vector<bool> blockOn;
    RECT primaryWork{};
    float primaryWidth = 0;
    float primaryHeight = 0;
    CornerLayout layout{};
};

// Tema escolhido em HKCU\Software\TemaBarra\FundoTema: "golden" = fundo.png ondulando ('g'); "loop" = alga <-> logo do
// Windows ('l'); "imagem" = a imagem de FundoImagem, parada ou ondulando com FundoOndular ('i'); "video" = o vídeo de
// FundoVideo, mudo e em loop ('v'); qualquer outra coisa (ou nada) = "nbhd", a tela de start retrô ('n').
static char ReadTheme() {
    std::wstring theme = SettingText(L"FundoTema", L"nbhd");
    static const std::pair<const wchar_t*, char> themes[] = {{L"golden", 'g'}, {L"loop", 'l'}, {L"imagem", 'i'}, {L"video", 'v'}};
    for (auto [name, code] : themes)
        if (!_wcsicmp(theme.c_str(), name)) return code;
    return 'n';
}

// Direct3D 11 com suporte a BGRA (para o Direct2D desenhar no mesmo quadro) e a vídeo (o tema de vídeo decodifica nele;
// sem esse suporte, abre sem) e a fábrica DXGI do adaptador.
static bool CreateDevice(Graphics& g) {
    bool created = false;
    const UINT attempts[] = {D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, D3D11_CREATE_DEVICE_BGRA_SUPPORT};
    for (UINT flags : attempts)
        if (!created) created = SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                                                            &g.device, nullptr, &g.context));
    if (!created) return false;

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

// Monitores e áreas úteis em coordenadas da nossa janela. O bloco do canto (música + indicadores) é desenhado no lugar
// do principal e deslocado para os outros (Wallpaper::PlaceBlock põe na posição escolhida).
static Monitors ReadMonitors(HWND parent, float scale) {
    std::vector<MONITORINFOEXW> infos;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM p) -> BOOL {
        MONITORINFOEXW info{};
        info.cbSize = sizeof info;
        GetMonitorInfoW(monitor, &info);
        ((std::vector<MONITORINFOEXW>*)p)->push_back(info);
        return TRUE;
    }, (LPARAM)&infos);

    MONITORINFO primary{sizeof primary};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &primary);

    Monitors monitors;
    for (auto& info : infos) {
        MapWindowPoints(HWND_DESKTOP, parent, (POINT*)&info.rcMonitor, 2);
        MapWindowPoints(HWND_DESKTOP, parent, (POINT*)&info.rcWork, 2);
        monitors.areas.push_back(info.rcMonitor);
        monitors.works.push_back(info.rcWork);
        monitors.devices.push_back(info.szDevice);
    }
    MapWindowPoints(HWND_DESKTOP, parent, (POINT*)&primary.rcWork, 2);

    monitors.primaryWork = primary.rcWork;
    monitors.primaryWidth = float(primary.rcMonitor.right - primary.rcMonitor.left);
    monitors.primaryHeight = float(primary.rcMonitor.bottom - primary.rcMonitor.top);
    monitors.layout = {0, 0, 0, 0, 36 * scale, scale};
    return monitors;
}

struct Block {
    float left;
    float top;
    float right;
    float bottom;
    float ringsY;
    float panelY;
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
    VideoTheme video;
    WidgetOptions options;
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
    bool dancing = false;
    bool imageFlow = false;
    char theme = 'n';
    std::wstring imageFile;
    std::wstring videoFile;
    std::wstring imageSource;
    std::wstring videoSource;

    void UpdateCapture(NowPlaying const& song, bool hidden, ULONGLONG now);
    bool CheckState(HWND parent, Surface const& surface, NowPlaying const& song, ULONGLONG now, bool& hidden);
    void UpdateWidgets(NowPlaying const& song, Monitors const& monitors, double now);
    float RingRow(CornerLayout const& layout) const;
    void PlaceBlock(Monitors& monitors) const;
    Block LayoutBlock(Monitors const& monitors) const;
    void DrawBackground(Surface const& surface, Monitors const& monitors, Block const& block, double now);
    void DrawOverlay(Surface const& surface, Monitors const& monitors, Block const& block, double now);
    void RenderFrame(Surface const& surface, Monitors& monitors, NowPlaying const& song);
    void UpdateButtons(HWND parent, Monitors const& monitors, bool hidden);
    void WaitNextFrame(double frameStart);
};

// Captura o som só enquanto o Spotify toca neste PC e alguém vê o fundo; se falhar, tenta de novo em 10 s.
void Wallpaper::UpdateCapture(NowPlaying const& song, bool hidden, ULONGLONG now) {
    if (song.playing && !song.elsewhere && !hidden && !capture && now >= nextCaptureAttempt) {
        capture = StartLoopbackCapture(audioClient);
        if (!capture) {
            audioClient = nullptr;
            nextCaptureAttempt = now + 10000;
        }
    } else if ((!song.playing || song.elsewhere || hidden) && audioClient) {
        audioClient->Stop();
        capture = nullptr;
        audioClient = nullptr;
    }
}

// A cada meio segundo: opções dos widgets, fundo visível? (FundoPausa = 0 só pausa com a tela bloqueada), cor da
// seleção, tema (e a imagem ou o vídeo dele), cores, captura de som e tamanho da área de trabalho. Resolução ou
// monitores mudaram (ou o Explorer levou o WorkerW junto): destrói a janela e devolve false para montar tudo de novo.
bool Wallpaper::CheckState(HWND parent, Surface const& surface, NowPlaying const& song, ULONGLONG now, bool& hidden) {
    options = ReadWidgetOptions();
    hidden = ComputeWallpaperHidden(options.pause);
    SetWallpaperHidden(hidden);
    RestoreSelectionColors();
    theme = ReadTheme();
    imageFile = SettingText(L"FundoImagem", L"");
    videoFile = SettingText(L"FundoVideo", L"");
    imageFlow = Setting(L"FundoOndular", 0) != 0;
    video.Pause(hidden);
    ReloadPalette();
    panel.options = options;
    card.showEqualizer = options.Equalizer();
    for (int i = 0; i < 4; i++) rings.show[i] = options.Ring(i);
    UpdateCapture(song, hidden, now);

    RECT current;
    bool sameSize = IsWindow(parent) && GetClientRect(parent, &current) && current.right == surface.size.right &&
                    current.bottom == surface.size.bottom;
    if (sameSize) return true;

    DestroyWindow(surface.window);
    return false;
}

// Equalizador (o som de verdade, ou dançando sozinho com o Spotify tocando em outro aparelho), cartão da música, a
// subida/descida do bloco e, a cada 1,5 s (só com o fundo visível), os anéis e a rede; o clima começa junto da
// primeira leitura.
void Wallpaper::UpdateWidgets(NowPlaying const& song, Monitors const& monitors, double now) {
    float dt = std::min(float(now - previousFrame), 0.1f);
    previousFrame = now;

    equalizer.Read(capture.Get());
    dancing = song.playing && song.elsewhere;
    if (dancing) equalizer.Simulate(now, dt);
    else if (card.alpha > 0 || capture) equalizer.Update(capture != nullptr, dt);
    card.Update(g.dc.Get(), g.dwrite.Get(), options.Music() ? song : NowPlaying{}, options.Next() ? CurrentNextTrack() : NextTrack{},
                monitors.layout, dt);
    presence = std::clamp(presence + (card.showing ? 2 : -2) * dt, 0.f, 1.f);

    if (now - lastReading <= 1.5) return;
    lastReading = now;
    stats.Read();
    StartWeatherService();
    double networkPercent = network.Read();
    rings.Update(stats, network, networkPercent);
}

// Altura da linha dos anéis (com as legendas), ou 0 com todos desligados.
float Wallpaper::RingRow(CornerLayout const& layout) const {
    return rings.Count() ? 2 * layout.ringRadius + 24 * layout.scale : 0;
}

// Põe o bloco na posição escolhida (Posicao: 0 a 8, da esquerda para a direita e de cima para baixo; 2 = canto de
// cima à direita), a Margem DIPs da borda na horizontal (44 no padrão, mais o que o painel passa do bloco, com o clima à
// esquerda) e 8 a menos na vertical. No meio e embaixo conta a altura de agora (sem música o bloco encolhe), então ele
// cresce e encolhe em volta do ponto escolhido. Nos outros monitores vai para o mesmo ponto da área útil (canto, meio
// da borda ou centro), menos nos de MonitoresOcultos. O tamanho (Escala) é um zoom em volta da quina do bloco que fica
// naquele ponto, então a distância da borda não muda.
void Wallpaper::PlaceBlock(Monitors& monitors) const {
    auto& l = monitors.layout;
    const float s = l.scale, k = options.size / 100.f, width = 380 * s;
    const float margin = options.margin * s + panel.Overflow() * k, vertical = std::max(options.margin - 8, 0) * s;
    const int column = options.position % 3, row = options.position / 3;
    RECT const& work = monitors.primaryWork;
    float p = presence * presence * (3 - 2 * presence);
    float height = p * (118 * s + card.NextRowHeight(s)) + RingRow(l) + panel.Height();
    float left = column == 0 ? work.left + margin : column == 1 ? (work.left + work.right - width) / 2 : work.right - margin - width;

    l.top = row == 0 ? work.top + vertical : row == 1 ? (work.top + work.bottom - height) / 2 : work.bottom - vertical - height;
    l.cardLeft = roundf(left);
    l.right = left + width;
    l.textLeft = l.cardLeft + CoverSize() + 16 * s;

    auto anchor = [&](RECT const& r) {
        return D2D1_POINT_2F{column == 0 ? r.left : column == 1 ? (r.left + r.right) / 2.f : r.right,
                             row == 0 ? r.top : row == 1 ? (r.top + r.bottom) / 2.f : r.bottom};
    };
    D2D1_POINT_2F home = anchor(work);
    D2D1_POINT_2F pivot{column == 0 ? l.cardLeft : column == 1 ? (l.cardLeft + l.right) / 2 : l.right,
                        row == 0 ? l.top : row == 1 ? l.top + height / 2 : l.top + height};
    monitors.corners.clear();
    monitors.blockOn.clear();
    for (size_t i = 0; i < monitors.works.size(); i++) {
        D2D1_POINT_2F there = anchor(monitors.works[i]);
        monitors.corners.push_back(D2D1::Matrix3x2F::Scale(k, k, pivot) *
                                   D2D1::Matrix3x2F::Translation(roundf(there.x - home.x), roundf(there.y - home.y)));
        monitors.blockOn.push_back(!options.Hidden(monitors.devices[i]));
    }
}

// Retângulo do bloco do canto (cartão + anéis + painel; sem música, só anéis e painel, que sobem para o lugar do
// cartão), para a área escura atrás dele, e onde ficam os anéis e o painel (que sobe para o lugar deles sem anéis).
Block Wallpaper::LayoutBlock(Monitors const& monitors) const {
    auto const& l = monitors.layout;
    const float s = l.scale, r = l.ringRadius;
    const int count = rings.Count();
    float a = card.alpha * card.alpha * (3 - 2 * card.alpha);
    float rowTop = l.top + presence * presence * (3 - 2 * presence) * (118 * s + card.NextRowHeight(s));
    float ringsY = roundf(rowTop + r + 2 * s), panelY = count ? ringsY + r + 22 * s : rowTop;
    float center = (l.cardLeft + l.right) / 2, halfWidth = count ? count * r + (count - 1) * 10 * s : (l.right - l.cardLeft) / 2;
    float top = count ? ringsY - r : rowTop;

    return {center - halfWidth + (l.cardLeft - center + halfWidth) * a, top + (l.top - top) * a,
            center + halfWidth + (l.right - center - halfWidth) * a, panelY + panel.Height(), ringsY, panelY};
}

// Direct3D: fundo preto e, em cada monitor, a imagem inteira ("golden" ondulando, "imagem" parada ou ondulando, ou o
// quadro do vídeo) ou as faixas do morph ("loop"), escurecidas atrás do bloco (Sombra). Cada tema só mantém os
// recursos dele carregados; trocar a imagem ou o vídeo no registro recarrega.
void Wallpaper::DrawBackground(Surface const& surface, Monitors const& monitors, Block const& block, double now) {
    float black[4] = {0, 0, 0, 1};
    auto* context = g.context.Get();
    context->ClearRenderTargetView(surface.target.Get(), black);
    context->OMSetRenderTargets(1, surface.target.GetAddressOf(), nullptr);

    std::wstring image = theme == 'g' ? ProjectPath(L"assets\\fundo.png") : theme == 'i' ? imageFile : theme == 'v' ? L"*video" : L"";
    if (image != imageSource) {
        golden.Unload();
        imageSource = image;
        if (theme == 'g' || theme == 'i') golden.Load(g.device.Get(), context, image);
    }
    std::wstring clip = theme == 'v' ? videoFile : L"";
    if (clip != videoSource) {
        videoSource = clip;
        if (clip.empty()) video.Close();
        else video.Open(g.device.Get(), clip);
    }
    if (auto* frame = theme == 'v' ? video.Frame() : nullptr) golden.Use(frame, video.Aspect());
    if (theme != 'n') nbhd.Release();
    if (theme == 'l' && !morphLoaded) morphLoaded = morph.Load(g.device.Get(), ProjectPath(L"assets\\fundo-morph.bin"));
    const float flow = theme == 'g' || (theme == 'i' && imageFlow) ? 1.f : 0.f, shadow = options.shadow / 100.f;

    for (size_t i = 0; i < monitors.areas.size(); i++) {
        RECT const& area = monitors.areas[i];
        auto const& corner = monitors.corners[i];
        D2D1_POINT_2F a = corner.TransformPoint({block.left, block.top}), b = corner.TransformPoint({block.right, block.bottom});
        const float away = -1e6f, shade[4] = {monitors.blockOn[i] ? a.x : away, monitors.blockOn[i] ? a.y : away, monitors.blockOn[i] ? b.x : away,
                                              monitors.blockOn[i] ? b.y : away};

        if (theme == 'l' && morphLoaded) {
            D3D11_VIEWPORT viewport{0, 0, float(surface.size.right), float(surface.size.bottom), 0, 1};
            context->RSSetViewports(1, &viewport);
            morph.Update(float(fmod(now - start, double(MorphTheme::kLoopSeconds))), float(area.right - area.left), float(area.bottom - area.top),
                         float(area.left), float(area.top));
            morph.Draw(context, float(surface.size.right), float(surface.size.bottom), shade, shadow);
        } else if (theme != 'l' && theme != 'n' && golden.Loaded()) {
            golden.Draw(context, area, float(fmod(now - start, 84000.0)), shade, flow, shadow);
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
        if (!monitors.blockOn[i]) continue;
        auto const& corner = monitors.corners[i];
        dc->SetTransform(corner);
        if (a > 0) card.Draw(dc, g.brush.Get(), l, equalizer, a, HoveredMediaButton(i), PressedMediaButton(i), ZoneHovered(Zone::Title, i),
                             ZoneHovered(Zone::Artist, i));
        rings.Draw(dc, g.brush.Get(), g.round.Get(), corner, l, block.ringsY);
        panel.Draw(dc, l.cardLeft, block.panelY, l.right - l.cardLeft, ZoneHovered(Zone::Light, i));
    }

    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    dc->EndDraw();
    dc->SetTarget(nullptr);
}

// O mesmo retângulo do bloco do canto em cada monitor (com o tamanho do bloco), em coordenadas de tela; vazio nos
// monitores sem o bloco.
static std::vector<RECT> PerMonitor(HWND parent, Monitors const& monitors, D2D1_RECT_F const& box) {
    std::vector<RECT> rects;
    for (size_t i = 0; i < monitors.corners.size(); i++) {
        D2D1_POINT_2F a = monitors.corners[i].TransformPoint({box.left, box.top}), b = monitors.corners[i].TransformPoint({box.right, box.bottom});
        RECT r{LONG(a.x), LONG(a.y), LONG(b.x), LONG(b.y)};
        MapWindowPoints(parent, HWND_DESKTOP, (POINT*)&r, 2);
        rects.push_back(monitors.blockOn[i] ? r : RECT{});
    }
    return rects;
}

// Janelas que pegam o clique, uma por monitor: sobre os botões, o nome da música e o do artista no cartão (com o
// cartão aparecendo) e sobre o valor da luz no JSON (com a luz lá). Só com o fundo visível.
void Wallpaper::UpdateButtons(HWND parent, Monitors const& monitors, bool hidden) {
    D2D1_RECT_F first = MediaButtonRect(monitors.layout, 0), last = MediaButtonRect(monitors.layout, kMediaButtons - 1), light = panel.LightRect();
    D2D1_RECT_F title = card.TitleRect(monitors.layout), artist = card.ArtistRect(monitors.layout);
    bool cardShown = card.alpha > 0.5f && !hidden;
    PlaceClickZones(Zone::Media, PerMonitor(parent, monitors, {first.left, first.top, last.right, last.bottom}), cardShown);
    PlaceClickZones(Zone::Title, PerMonitor(parent, monitors, title), cardShown && title.right > title.left);
    PlaceClickZones(Zone::Artist, PerMonitor(parent, monitors, artist), cardShown && artist.right > artist.left);
    PlaceClickZones(Zone::Light, PerMonitor(parent, monitors, light), light.right > light.left && !hidden);
}

// Um quadro inteiro.
void Wallpaper::RenderFrame(Surface const& surface, Monitors& monitors, NowPlaying const& song) {
    double now = Now();
    UpdateWidgets(song, monitors, now);
    PlaceBlock(monitors);

    Block block = LayoutBlock(monitors);
    DrawBackground(surface, monitors, block, now);
    DrawOverlay(surface, monitors, block, now);
    surface.swapChain->Present(1, 0);
    WaitNextFrame(now);
}

// 60 fps enquanto o equalizador anda ou no tema de vídeo; senão ~30 (o movimento é lento, mais seria desperdício). O
// monitor é de 239 Hz, então quem dita o ritmo é este timer de alta resolução (o Sleep só tem resolução de ~16 ms).
void Wallpaper::WaitNextFrame(double frameStart) {
    double frame = capture || dancing || theme == 'v' ? 1 / 60.0 : 1 / 30.0;
    LARGE_INTEGER due{.QuadPart = -LONGLONG(1e7 * std::max(0.001, frame - (Now() - frameStart)))};

    SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
    WaitForSingleObject(timer, 100);
}

// Uma sessão da janela no WorkerW: desenha ao menos um quadro (mesmo se já abrir pausado) e depois só com alguém vendo
// o fundo; pausado, fica em 0% de GPU.
void Wallpaper::RunSession(HWND parent, Surface const& surface) {
    Monitors monitors = ReadMonitors(parent, g.scale);
    PlaceBlock(monitors);
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
