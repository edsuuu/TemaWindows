#include "wallpaper/folder_icons.h"
#include "wallpaper/media.h"
#include "wallpaper/next_track.h"
#include "wallpaper/renderer.h"
#include "common/registry.h"

#include <objbase.h>

// FundoVivo.exe (HKLM Run, para todos os usuários): papel de parede animado com os widgets no canto. Sai na hora se
// estiver desligado (HKCU\Software\TemaBarra\FundoVivo = 0) ou se já houver um rodando nesta sessão. Também põe o
// ícone com degradê nas pastas da área de trabalho (Pastas = 0 desliga).
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    if (!Enabled(L"FundoVivo")) return 0;

    CreateMutexW(nullptr, TRUE, L"Local\\FundoVivo");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    StartMediaWatcher();
    StartNextTrackService();
    if (Enabled(L"Pastas")) StartFolderIconService();
    return RunWallpaper(instance);
}
