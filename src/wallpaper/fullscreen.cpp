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

// A janela da frente cobre o monitor inteiro (jogo em janela sem borda, vídeo em tela cheia)? Maximizada não conta
// (fica acima da barra), nem a área de trabalho.
static bool ForegroundCoversMonitor() {
    HWND window = GetForegroundWindow();
    wchar_t name[16] = {};
    GetClassNameW(window, name, 16);
    if (!window || IsZoomed(window) || !wcscmp(name, L"WorkerW") || !wcscmp(name, L"Progman")) return false;

    RECT rect;
    MONITORINFO monitor{sizeof monitor};
    if (!GetWindowRect(window, &rect) || !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return false;
    return rect.left <= monitor.rcMonitor.left && rect.top <= monitor.rcMonitor.top && rect.right >= monitor.rcMonitor.right &&
           rect.bottom >= monitor.rcMonitor.bottom;
}

// Jogo ou app em tela cheia (ou apresentação)? O overlay do UnkvoidClips não conta. QUNS_BUSY só com a janela da
// frente cobrindo o monitor: o Windows também dá BUSY para qualquer janela por cima de tudo do tamanho da tela, como o
// flash invisível (click-through, sem foco) que o Windows MCP desenha a cada print, e o player sumia da barra.
// ponytail: com o overlay aberto por cima de um jogo, o fundo volta a desenhar atrás do jogo até o overlay fechar.
bool FullScreenApp() {
    QUERY_USER_NOTIFICATION_STATE state;
    if (FAILED(SHQueryUserNotificationState(&state))) return false;
    bool busy = state == QUNS_BUSY && ForegroundCoversMonitor();
    return (busy || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE) && !ClipsInFront();
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
