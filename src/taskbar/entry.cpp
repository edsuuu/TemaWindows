#include "taskbar/taskbar.h"
#include "common/log.h"
#include "common/registry.h"
#include "common/xaml_tap.h"
#include "common/xaml_tree.h"

#include <winrt/Windows.UI.Xaml.Automation.h>
#include <string_view>
#include <unordered_set>

const CLSID kTapClsid = {0x6b1f4e2a, 0x9c3d, 0x4f7b, {0x8a, 0x15, 0x2e, 0x9d, 0x0c, 0x4b, 0x7a, 0x31}};

static std::unordered_set<void*> g_hooked;

// Nada a preparar: o Explorer não precisa de estado por processo.
void OnTapStarted() {}

// Aplica agora (elemento já na árvore) e de novo a cada Loaded (elemento recriado, como a caixa de pesquisa ao trocar o
// modo), sem deixar exceção escapar: ela derrubaria o Explorer.
template <class Element, class Apply>
static void ApplyNowAndOnLoad(Element const& element, Apply apply, const wchar_t* what) {
    auto run = [element, apply, what](auto&&...) {
        try {
            apply(element);
        } catch (...) {
            Log(L"erro em %s: %08X", what, winrt::to_hresult());
        }
    };
    run();
    element.Loaded(run);
}

// Border com esse handle, aplicando o recurso agora e a cada Loaded.
template <class Apply>
static void ApplyToBorder(InstanceHandle handle, Apply apply, const wchar_t* what) {
    if (auto border = ElementFromHandle(handle).try_as<Controls::Border>()) ApplyNowAndOnLoad(border, apply, what);
}

// O fundo chamado BackgroundElement também existe nos botões de apps: o vidro só vale para o da caixa de pesquisa.
static void ApplySearchGlassIfSearchBox(Controls::Border const& border) {
    if (FindAncestor(border, L"SearchUx.SearchUI.SearchButtonRootGrid", 3)) ApplySearchGlass(border);
}

// Ícone da barra: no botão Iniciar vira o logo (Logo); nos botões de apps, liga uma vez só o indicador cinza da lista.
static void StyleIcon(FrameworkElement const& icon) {
    auto startButton = FindAncestor(icon, L"Taskbar.ExperienceToggleButton", 6);
    if (startButton && Automation::AutomationProperties::GetAutomationId(startButton) == L"StartButton") {
        if (Enabled(L"Logo")) ApplyStartLogo(icon);
        return;
    }

    auto list = FindAncestor(icon, L"Microsoft.UI.Xaml.Controls.ItemsRepeater", 4);
    if (list && FindAncestor(icon, L"Taskbar.TaskListButton", 3) && g_hooked.insert(winrt::get_abi(list)).second) HookTaskButtons(list);
}

// Aplica no ícone agora (enumeração inicial) e a cada Loaded (barra recriada), registrando o Loaded uma vez só.
static void HookIcon(InstanceHandle handle) {
    auto icon = FrameworkElementFromHandle(handle);
    if (!icon) return;

    auto run = [icon](auto&&...) {
        try {
            StyleIcon(icon);
        } catch (...) {
            Log(L"erro no ícone: %08X", winrt::to_hresult());
        }
    };
    run();
    if (g_hooked.insert(winrt::get_abi(icon)).second) icon.Loaded(run);
}

// Sem o anel de foco branco do XAML (2 px, aparece ao abrir Iniciar, pesquisa e painéis) nos botões da barra, da
// bandeja, da pesquisa e dos ícones ocultos: ele pediu. Quem usa o teclado perde o anel nesses botões.
static void RemoveFocusRing(std::wstring_view type, InstanceHandle handle) {
    if (!type.starts_with(L"Taskbar.") && !type.starts_with(L"SystemTray.") && !type.starts_with(L"SearchUx.")) return;

    if (auto control = FrameworkElementFromHandle(handle).try_as<Controls::Control>()) control.UseSystemFocusVisuals(false);
}

// Cada elemento novo da árvore XAML da barra: fundo com blur e botão "mostrar área de trabalho" invisível (continua
// clicável) com Fundo; vidro no painel dos ícones ocultos e na caixa de pesquisa com Vidro; e os ícones.
void OnElementAdded(ParentChildRelation const&, VisualElement const& element) {
    std::wstring_view type = element.Type ? element.Type : L"", name = element.Name ? element.Name : L"";
    RemoveFocusRing(type, element.Handle);

    if (type == L"Taskbar.TaskbarBackground" && Enabled(L"Fundo")) {
        ApplyNowAndOnLoad(FrameworkElementFromHandle(element.Handle), ApplyBlurBackground, L"fundo");
    } else if (type == L"SystemTray.ShowDesktopButton" && Enabled(L"Fundo")) {
        FrameworkElementFromHandle(element.Handle).Opacity(0);
    } else if (name == L"OverflowFlyoutBackgroundBorder" && Enabled(L"Vidro")) {
        ApplyToBorder(element.Handle, ApplyOverflowGlass, L"vidro dos ícones ocultos");
    } else if (name == L"BackgroundElement" && Enabled(L"Vidro")) {
        ApplyToBorder(element.Handle, ApplySearchGlassIfSearchBox, L"vidro da pesquisa");
    } else if (name == L"Icon") {
        HookIcon(element.Handle);
    }
}

// Espera a barra existir e devolve o PID do Explorer dono dela.
static DWORD WaitForExplorer() {
    HWND tray;
    while (!(tray = FindWindowW(L"Shell_TrayWnd", nullptr))) Sleep(1000);

    DWORD pid;
    GetWindowThreadProcessId(tray, &pid);
    return pid;
}

// Tenta injetar por até ~1 minuto, dando tempo da barra XAML subir antes de cada tentativa.
static HRESULT InjectWithRetry(DWORD pid) {
    HRESULT hr = E_FAIL;
    for (int attempt = 0; attempt < 30 && FAILED(hr); attempt++) {
        Sleep(2000);
        hr = InjectTap(pid);
    }
    return hr;
}

// rundll32 TemaBarra.dll,Run: injeta no Explorer (e só nele) e reinjeta sempre que ele reiniciar. Se o Explorer cair
// duas vezes seguidas logo depois da injeção (provavelmente por causa da DLL), para de injetar em vez de derrubar a
// barra em loop.
extern "C" __declspec(dllexport) void CALLBACK Run(HWND, HINSTANCE, LPSTR, int) {
    for (int crashes = 0;;) {
        DWORD pid = WaitForExplorer();
        HANDLE explorer = OpenProcess(SYNCHRONIZE, FALSE, pid);
        Log(L"injetado no explorer %lu: %08X", pid, InjectWithRetry(pid));
        if (!explorer) return;

        ULONGLONG start = GetTickCount64();
        WaitForSingleObject(explorer, INFINITE);
        CloseHandle(explorer);

        crashes = GetTickCount64() - start < 120000 ? crashes + 1 : 0;
        if (crashes >= 2) {
            Log(L"explorer caiu 2x logo após a injeção; parei de injetar");
            return;
        }
    }
}
