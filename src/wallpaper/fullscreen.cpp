#include "wallpaper/fullscreen.h"

#include <dwmapi.h>
#include <shellapi.h>

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
bool FullScreenApp() {
    QUERY_USER_NOTIFICATION_STATE state;
    return SUCCEEDED(SHQueryUserNotificationState(&state)) &&
           (state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE) && !ClipsInFront();
}

// Monitores com uma janela maximizada (visível e não oculta pelo DWM) por cima: a área de trabalho deles some atrás.
std::vector<HMONITOR> CoveredMonitors() {
    std::vector<HMONITOR> covered;

    EnumWindows([](HWND window, LPARAM p) -> BOOL {
        BOOL cloaked = FALSE;
        if (!IsWindowVisible(window) || !IsZoomed(window)) return TRUE;

        DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof cloaked);
        if (!cloaked) ((std::vector<HMONITOR>*)p)->push_back(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST));
        return TRUE;
    }, (LPARAM)&covered);
    return covered;
}
