#include "wallpaper/desktop.h"
#include "wallpaper/fullscreen.h"

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

// Todos os monitores cobertos por janelas maximizadas?
static bool AllMonitorsCovered() {
    std::vector<HMONITOR> all, covered = CoveredMonitors();

    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM p) -> BOOL {
        ((std::vector<HMONITOR>*)p)->push_back(monitor);
        return TRUE;
    }, (LPARAM)&all);

    for (auto monitor : all)
        if (std::find(covered.begin(), covered.end(), monitor) == covered.end()) return false;
    return true;
}

// Ninguém está vendo o fundo (tela bloqueada, tela cheia ou tudo coberto)? Então não gasta GPU, som nem rede. Sem
// `apps` (FundoPausa = 0) só a tela bloqueada pausa.
bool ComputeWallpaperHidden(bool apps) {
    return SessionLocked() || (apps && (FullScreenApp() || AllMonitorsCovered()));
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
