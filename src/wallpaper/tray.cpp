#include "wallpaper/tray.h"
#include "common/paths.h"

#include <objbase.h>
#include <shellapi.h>
#include <windowsx.h>
#include <string>
#include <thread>

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kOpen = 1;
constexpr UINT kQuit = 2;

static NOTIFYICONDATAW tray{sizeof tray};
static UINT taskbarCreated;

// Abre o app de configuração ao lado (WallgetConfig.exe instalado, FundoConfig.exe no desenvolvimento); se ele já
// estiver aberto, ele mesmo só vem para a frente.
static void OpenConfig() {
    std::wstring app = ProjectPath(L"bin\\WallgetConfig.exe");
    if (GetFileAttributesW(app.c_str()) == INVALID_FILE_ATTRIBUTES) app = ProjectPath(L"bin\\FundoConfig.exe");
    AllowSetForegroundWindow(ASFW_ANY);
    ShellExecuteW(nullptr, L"open", app.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// Sai de vez (volta quando o Windows iniciar ou o app abrir): tira o ícone; com a nossa janela fechada, o WorkerW já
// mostra o papel de parede do Windows sozinho.
static void Quit() {
    Shell_NotifyIconW(NIM_DELETE, &tray);
    ExitProcess(0);
}

// Menu do botão direito no ícone, escuro como o resto do Windows.
static void ShowMenu(HWND window, int x, int y) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kOpen, L"Abrir o Wallget");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kQuit, L"Sair");
    SetMenuDefaultItem(menu, kOpen, FALSE);

    SetForegroundWindow(window);  // sem isso o menu não fecha ao clicar fora
    UINT chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, x, y, 0, window, nullptr);
    PostMessageW(window, WM_NULL, 0, 0);  // sem isso o menu da bandeja pisca e some na segunda vez (documentação do TrackPopupMenu)
    DestroyMenu(menu);
    if (chosen == kOpen) OpenConfig();
    if (chosen == kQuit) Quit();
}

// Clique (ou Enter) no ícone abre o app; botão direito, o menu. O Explorer reiniciando apaga os ícones da bandeja:
// TaskbarCreated põe o nosso de volta.
static LRESULT CALLBACK TrayProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == taskbarCreated) {
        Shell_NotifyIconW(NIM_ADD, &tray);
        Shell_NotifyIconW(NIM_SETVERSION, &tray);
        return 0;
    }
    if (message == kTrayMessage) {
        switch (LOWORD(lParam)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            OpenConfig();
            break;
        case WM_CONTEXTMENU:
            ShowMenu(window, GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam));
            break;
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

// Menus escuros num app Win32: SetPreferredAppMode (uxtheme, ordinal 135, sem nome público; o Bloco de Notas e o
// Explorer usam) com 1 = segue o tema do Windows. Sem ela (outra versão do Windows), o menu fica claro.
static void FollowDarkMode() {
    using SetPreferredAppMode = int(WINAPI*)(int);
    if (HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
        if (auto set = (SetPreferredAppMode)GetProcAddress(uxtheme, MAKEINTRESOURCEA(135))) set(1);
}

// Ícone do Wallget na bandeja (perto do relógio, junto dos outros apps em segundo plano), numa thread própria com
// uma janela escondida que recebe os cliques.
void StartTrayIcon(HINSTANCE instance) {
    std::thread([instance] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // o ShellExecute pede
        FollowDarkMode();
        taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        WNDCLASSW windowClass{0, TrayProc, 0, 0, instance, nullptr, nullptr, nullptr, nullptr, L"WallgetBandeja"};
        RegisterClassW(&windowClass);
        HWND window = CreateWindowExW(0, L"WallgetBandeja", L"Wallget", 0, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);

        tray.hWnd = window;
        tray.uID = 1;
        tray.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
        tray.uCallbackMessage = kTrayMessage;
        tray.hIcon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
        tray.uVersion = NOTIFYICON_VERSION_4;
        wcscpy_s(tray.szTip, L"Wallget");
        Shell_NotifyIconW(NIM_ADD, &tray);
        Shell_NotifyIconW(NIM_SETVERSION, &tray);

        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) DispatchMessageW(&message);
    }).detach();
}
