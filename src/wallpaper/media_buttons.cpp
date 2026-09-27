#include "wallpaper/media_buttons.h"
#include "wallpaper/media.h"

#include <windowsx.h>
#include <algorithm>
#include <atomic>

static std::vector<HWND> g_windows;
static std::atomic<int> g_hovered{-1};
static std::atomic<int> g_pressed{-1};

// Botão sob o ponto (0 voltar, 1 tocar/pausar, 2 avançar), somado a 3 x o monitor da janela; -1 fora dela.
static int ButtonAt(HWND window, LPARAM point) {
    RECT client;
    GetClientRect(window, &client);
    POINT p{GET_X_LPARAM(point), GET_Y_LPARAM(point)};
    if (!PtInRect(&client, p) || client.right <= 0) return -1;

    return int(GetWindowLongPtrW(window, GWLP_USERDATA)) * 3 + std::clamp(int(p.x * 3 / client.right), 0, 2);
}

// Mouse nas janelas dos botões: hover, e apertar e soltar em cima do mesmo botão manda o comando para o Spotify. Não
// ativa o Progman, para não mexer na seleção dos ícones.
static LRESULT CALLBACK ButtonProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT track{sizeof track, TME_LEAVE, window, 0};
        TrackMouseEvent(&track);
        g_hovered = ButtonAt(window, lParam);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_hovered = -1;
        return 0;
    case WM_LBUTTONDOWN:
        g_pressed = ButtonAt(window, lParam);
        SetCapture(window);
        return 0;
    case WM_LBUTTONUP: {
        int button = ButtonAt(window, lParam);
        ReleaseCapture();
        if (button >= 0 && button == g_pressed.exchange(-1)) SendMediaCommand(button % 3);
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

// Janela invisível filha do Progman, acima da lista de ícones: em camada com alfa 1 (não aparece, mas recebe o
// clique; alfa 0 deixaria o clique passar). O desenho dos botões fica por conta do papel de parede.
static HWND CreateButtonWindow(HWND progman, int monitor) {
    static bool registered = [] {
        WNDCLASSW windowClass{0, ButtonProc, 0, 0, GetModuleHandleW(nullptr), nullptr, LoadCursorW(nullptr, (LPCWSTR)IDC_HAND),
                              (HBRUSH)GetStockObject(BLACK_BRUSH), nullptr, L"FundoVivoBotoes"};
        return RegisterClassW(&windowClass) != 0;
    }();
    if (!registered) return nullptr;

    HWND window = CreateWindowExW(WS_EX_LAYERED, L"FundoVivoBotoes", L"", WS_CHILD, 0, 0, 1, 1, progman, nullptr, GetModuleHandleW(nullptr),
                                  nullptr);
    if (!window) return nullptr;

    SetLayeredWindowAttributes(window, 0, 1, LWA_ALPHA);
    SetWindowLongPtrW(window, GWLP_USERDATA, monitor);
    return window;
}

// Põe as janelas dos botões nos retângulos de cada monitor (coordenadas de tela) e as mostra ou esconde. Recria as que
// o Explorer levou junto ao reiniciar e as recoloca acima da lista de ícones a cada chamada.
void PlaceMediaButtons(std::vector<RECT> const& rects, bool visible) {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) return;

    for (size_t i = rects.size(); i < g_windows.size(); i++) DestroyWindow(g_windows[i]);
    g_windows.resize(rects.size());
    if (!visible) g_hovered = g_pressed = -1;

    for (size_t i = 0; i < rects.size(); i++) {
        HWND& window = g_windows[i];
        if (!IsWindow(window) || GetParent(window) != progman) window = CreateButtonWindow(progman, int(i));
        if (!window) continue;

        RECT r = rects[i];
        MapWindowPoints(HWND_DESKTOP, progman, (POINT*)&r, 2);
        SetWindowPos(window, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
}

// Botão com o mouse em cima neste monitor (0 a 2), ou -1.
int HoveredMediaButton(int monitor) {
    int hovered = g_hovered;
    return hovered >= 0 && hovered / 3 == monitor ? hovered % 3 : -1;
}

// Botão apertado neste monitor (0 a 2), ou -1.
int PressedMediaButton(int monitor) {
    int pressed = g_pressed;
    return pressed >= 0 && pressed / 3 == monitor ? pressed % 3 : -1;
}
