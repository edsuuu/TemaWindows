#include "wallpaper/desktop.h"
#include "wallpaper/folder_icons.h"
#include "wallpaper/light_switch.h"
#include "wallpaper/media.h"
#include "wallpaper/next_track.h"
#include "wallpaper/renderer.h"
#include "wallpaper/tray.h"
#include "common/registry.h"

#include <objbase.h>

// Instalado (Wallget.exe): já na primeira vez que abre, passa a iniciar com o Windows (HKCU Run "Wallget"), a não ser
// que o app tenha desligado (IniciarComWindows = 0).
static void StartWithWindows() {
    wchar_t path[MAX_PATH];
    DWORD size = 0;
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (_wcsicmp(wcsrchr(path, L'\\') + 1, L"Wallget.exe") || !Enabled(L"IniciarComWindows") ||
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"Wallget", RRF_RT_REG_SZ, nullptr, nullptr,
                     &size) == ERROR_SUCCESS)
        return;

    std::wstring command = L"\"" + std::wstring(path) + L"\"";
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"Wallget", REG_SZ, command.c_str(),
                    DWORD((command.size() + 1) * sizeof(wchar_t)));
}

// FundoVivo.exe (HKLM Run, para todos os usuários; Wallget.exe no instalador): papel de parede animado com os widgets
// no canto. Sai na hora se estiver desligado (HKCU\Software\TemaBarra\FundoVivo = 0) ou se já houver um rodando nesta
// sessão. Também põe o ícone com degradê nas pastas da área de trabalho (Pastas = 0 desliga) e o dele na bandeja.
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    if (!Enabled(L"FundoVivo")) return 0;

    CreateMutexW(nullptr, TRUE, L"Local\\FundoVivo");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    StartWithWindows();

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    StartMediaWatcher(WallpaperHidden);
    StartLightService();
    StartNextTrackService();
    StartFolderIconService();
    StartTrayIcon(instance);
    return RunWallpaper(instance);
}
