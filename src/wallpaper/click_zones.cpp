#include "wallpaper/click_zones.h"
#include "wallpaper/light_switch.h"
#include "wallpaper/media.h"
#include "wallpaper/next_track.h"

#include <windowsx.h>
#include <algorithm>
#include <atomic>

static std::vector<HWND> g_windows[4];
static std::atomic<int> g_hovered{-1};
static std::atomic<int> g_pressed{-1};

// Onde o ponto cai: na zona dos botões da música, o botão (0 aleatória, 1 voltar, 2 tocar/pausar, 3 avançar) somado a
// 4 x o monitor; nas outras (luz, nome da música, artista), o monitor. -1 fora da janela. A zona vai em cima (zona x
// 100) para hover e clique não se confundirem entre elas.
static int TargetAt(HWND window, LPARAM point) {
    RECT client;
    GetClientRect(window, &client);
    POINT p{GET_X_LPARAM(point), GET_Y_LPARAM(point)};
    if (!PtInRect(&client, p) || client.right <= 0) return -1;

    int data = int(GetWindowLongPtrW(window, GWLP_USERDATA)), zone = data / 16, monitor = data % 16;
    if (zone != int(Zone::Media)) return zone * 100 + monitor;
    return monitor * kMediaButtons + std::clamp(int(p.x * kMediaButtons / client.right), 0, kMediaButtons - 1);
}

// Clique completo (apertar e soltar no mesmo alvo): botão da música vira comando para o Spotify, o valor da luz liga
// ou desliga o interruptor, e o nome da música ou do artista abre ele no Spotify.
static void Activate(int target) {
    switch (Zone(target / 100)) {
    case Zone::Media: SendMediaCommand(target % kMediaButtons); break;
    case Zone::Light: ToggleLight(); break;
    case Zone::Title: OpenInSpotify(false); break;
    case Zone::Artist: OpenInSpotify(true); break;
    }
}

// Mouse nas janelas de clique: hover, e apertar e soltar em cima do mesmo alvo ativa. Não ativa o Progman, para não
// mexer na seleção dos ícones.
static LRESULT CALLBACK ZoneProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT track{sizeof track, TME_LEAVE, window, 0};
        TrackMouseEvent(&track);
        g_hovered = TargetAt(window, lParam);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_hovered = -1;
        return 0;
    case WM_LBUTTONDOWN:
        g_pressed = TargetAt(window, lParam);
        SetCapture(window);
        return 0;
    case WM_LBUTTONUP: {
        int target = TargetAt(window, lParam);
        ReleaseCapture();
        if (target >= 0 && target == g_pressed.exchange(-1)) Activate(target);
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

// Janela invisível filha do Progman, acima da lista de ícones: em camada com alfa 1 (não aparece, mas recebe o
// clique; alfa 0 deixaria o clique passar). O desenho fica por conta do papel de parede.
static HWND CreateZoneWindow(HWND progman, Zone zone, int monitor) {
    static bool registered = [] {
        WNDCLASSW windowClass{0, ZoneProc, 0, 0, GetModuleHandleW(nullptr), nullptr, LoadCursorW(nullptr, (LPCWSTR)IDC_HAND),
                              (HBRUSH)GetStockObject(BLACK_BRUSH), nullptr, L"FundoVivoBotoes"};
        return RegisterClassW(&windowClass) != 0;
    }();
    if (!registered) return nullptr;

    HWND window = CreateWindowExW(WS_EX_LAYERED, L"FundoVivoBotoes", L"", WS_CHILD, 0, 0, 1, 1, progman, nullptr, GetModuleHandleW(nullptr),
                                  nullptr);
    if (!window) return nullptr;

    SetLayeredWindowAttributes(window, 0, 1, LWA_ALPHA);
    SetWindowLongPtrW(window, GWLP_USERDATA, int(zone) * 16 + monitor);
    return window;
}

// Põe as janelas de uma zona nos retângulos de cada monitor (coordenadas de tela) e as mostra ou esconde. Recria as
// que o Explorer levou junto ao reiniciar e as recoloca acima da lista de ícones a cada chamada.
void PlaceClickZones(Zone zone, std::vector<RECT> const& rects, bool visible) {
    HWND progman = FindWindowW(L"Progman", nullptr);
    auto& windows = g_windows[int(zone)];
    if (!progman) return;

    for (size_t i = rects.size(); i < windows.size(); i++) DestroyWindow(windows[i]);
    windows.resize(rects.size());
    if (!visible && g_hovered / 100 == int(zone)) g_hovered = g_pressed = -1;

    for (size_t i = 0; i < rects.size(); i++) {
        HWND& window = windows[i];
        if (!IsWindow(window) || GetParent(window) != progman) window = CreateZoneWindow(progman, zone, int(i));
        if (!window) continue;

        RECT r = rects[i];
        MapWindowPoints(HWND_DESKTOP, progman, (POINT*)&r, 2);
        SetWindowPos(window, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
}

// Botão da música com o mouse em cima neste monitor (0 a 3), ou -1.
int HoveredMediaButton(int monitor) {
    int hovered = g_hovered;
    return hovered >= 0 && hovered / 100 == int(Zone::Media) && hovered % 100 / kMediaButtons == monitor ? hovered % kMediaButtons : -1;
}

// Botão da música apertado neste monitor (0 a 3), ou -1.
int PressedMediaButton(int monitor) {
    int pressed = g_pressed;
    return pressed >= 0 && pressed / 100 == int(Zone::Media) && pressed % 100 / kMediaButtons == monitor ? pressed % kMediaButtons : -1;
}

// O mouse está em cima da zona (luz, nome da música ou artista) neste monitor?
bool ZoneHovered(Zone zone, int monitor) {
    return g_hovered == int(zone) * 100 + monitor;
}
