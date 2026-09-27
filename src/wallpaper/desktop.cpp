#include "wallpaper/desktop.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <wtsapi32.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <vector>

static std::atomic<bool> g_hidden{false};

// Escala do DPI do sistema (1 = 96 DPI).
float DpiScale() {
    return GetDpiForSystem() / 96.f;
}

// Janela onde o papel de parede mora: o WorkerW atrás dos ícones da área de trabalho (o mesmo truque do Lively e do
// Wallpaper Engine). A mensagem 0x052C pede ao Explorer para criar o WorkerW; no Windows 11 24H2+ ele é filho do
// Progman, antes era irmão da janela que tem os ícones.
HWND FindWallpaperParent() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) return nullptr;

    SendMessageTimeoutW(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, nullptr);
    if (HWND worker = FindWindowExW(progman, nullptr, L"WorkerW", nullptr)) return worker;

    HWND worker = nullptr;
    EnumWindows([](HWND top, LPARAM out) -> BOOL {
        if (FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr)) *(HWND*)out = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
        return TRUE;
    }, (LPARAM)&worker);
    return worker;
}

// Tela bloqueada?
static bool SessionLocked() {
    WTSINFOEXW* info = nullptr;
    DWORD bytes;
    if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSSessionInfoEx, (LPWSTR*)&info, &bytes)) return false;

    bool locked = info->Level == 1 && info->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_LOCK;
    WTSFreeMemory(info);
    return locked;
}

// A janela da frente é do UnkvoidClips? O overlay do Alt+Z cobre a tela inteira, mas é translúcido: o fundo aparece
// atrás dele.
static bool ClipsInFront() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;

    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    bool clips = QueryFullProcessImageNameW(process, 0, path, &size) && !_wcsicmp(wcsrchr(path, L'\\') + 1, L"UnkvoidClips.exe");
    CloseHandle(process);
    return clips;
}

// Jogo ou app em tela cheia (ou apresentação)? O overlay do UnkvoidClips não conta.
// ponytail: com o overlay aberto por cima de um jogo, o fundo volta a desenhar atrás do jogo até o overlay fechar.
static bool FullScreenApp() {
    QUERY_USER_NOTIFICATION_STATE state;
    return SUCCEEDED(SHQueryUserNotificationState(&state)) &&
           (state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE) && !ClipsInFront();
}

// Todos os monitores cobertos por janelas maximizadas (visíveis e não ocultas pelo DWM)?
static bool AllMonitorsCovered() {
    struct { std::vector<HMONITOR> all, covered; } monitors;

    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM p) -> BOOL {
        ((decltype(monitors)*)p)->all.push_back(monitor);
        return TRUE;
    }, (LPARAM)&monitors);

    EnumWindows([](HWND window, LPARAM p) -> BOOL {
        BOOL cloaked = FALSE;
        DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof cloaked);
        if (IsWindowVisible(window) && IsZoomed(window) && !cloaked)
            ((decltype(monitors)*)p)->covered.push_back(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST));
        return TRUE;
    }, (LPARAM)&monitors);

    for (auto monitor : monitors.all)
        if (std::find(monitors.covered.begin(), monitors.covered.end(), monitor) == monitors.covered.end()) return false;
    return true;
}

// Ninguém está vendo o fundo (tela bloqueada, tela cheia ou tudo coberto)? Então não gasta GPU, som nem rede.
bool ComputeWallpaperHidden() {
    return SessionLocked() || FullScreenApp() || AllMonitorsCovered();
}

// Publica o estado para as threads de mídia e de clima.
void SetWallpaperHidden(bool hidden) {
    g_hidden = hidden;
}

// Último estado publicado pelo quadro.
bool WallpaperHidden() {
    return g_hidden;
}

// Cor da seleção: algo (tema/Explorer) às vezes reaplica as cores padrão na sessão e o retângulo de seleção volta a
// ficar azul, mesmo com o registro certo. Confere Hilight e HotTrackingColor com o registro e reaplica se mudaram.
void RestoreSelectionColors() {
    const int ids[2] = {COLOR_HIGHLIGHT, COLOR_HOTLIGHT};
    const wchar_t* names[2] = {L"Hilight", L"HotTrackingColor"};

    for (int i = 0; i < 2; i++) {
        wchar_t value[32];
        DWORD size = sizeof value;
        int r, g, b;
        if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Colors", names[i], RRF_RT_REG_SZ, nullptr, value, &size) ||
            swscanf_s(value, L"%d %d %d", &r, &g, &b) != 3)
            continue;

        COLORREF color = RGB(r, g, b);
        if (GetSysColor(ids[i]) != color) SetSysColors(1, &ids[i], &color);
    }
}
