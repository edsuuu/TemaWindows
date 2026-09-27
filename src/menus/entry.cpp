#include "menus/menus.h"
#include "common/log.h"
#include "common/registry.h"
#include "common/xaml_tap.h"

const CLSID kTapClsid = {0xc4e2a7d1, 0x5b3f, 0x4e8a, {0x9d, 0x62, 0x7f, 0x1b, 0x0a, 0x3c, 0x5e, 0x94}};

static constexpr std::wstring_view kStyledNames =
    L"|AcrylicBorder|MediaTransportControlsRegion|ControlCenterRegion|NotificationCenterGrid|"
    L"CalendarCenterGrid|AcrylicOverlay|MediaTransportControlsRoot|CalendarControlScrollViewer|FocusGrid|StartDropShadow|"
    L"MainMenuHighContrastBorder|ItemOpaquePlating|BorderElement|AppBorder|AccentAppBorder|LayerBorder|AccentLayerBorder|dropshadow|"
    L"HCBorder|TaskbarSearchBackground|MainMenu|MainContent|StartFrame|UserTileNameText|PinnedListHeaderText|AllListHeadingText|"
    L"ZoomedOutHeading|PlaceholderText|PlaceholderTextContentPresenter|StartMenuPinnedList|TopLevelSuggestionsContainerParent|"
    L"SearchIconOn|SearchIconOff|SearchIconPlayer|SlidersGroup|FooterGrid|BackButton|HeaderToggleSwitch|";

static bool g_enabled;
static bool g_startProcess;
static bool g_concept;

// Lê as chaves quando a DLL entra no processo e descobre em qual host ela está: o layout do conceito só vale no Iniciar
// e na pesquisa (MenusConceito).
void OnTapStarted() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const wchar_t* name = wcsrchr(exe, L'\\') + 1;

    g_enabled = Enabled(L"Menus");
    g_startProcess = !_wcsicmp(name, L"StartMenuExperienceHost.exe");
    g_concept = (g_startProcess || !_wcsicmp(name, L"SearchHost.exe")) && Setting(L"MenusConceito", 1);
    Log(L"TAP carregado (Menus=%d)", g_enabled);
}

// Elemento que interessa: um dos nomes da lista (filtro rápido antes de pegar o objeto) ou um Border sem nome.
static bool IsStyled(std::wstring_view name, std::wstring_view type) {
    return name.empty() ? type == L"Windows.UI.Xaml.Controls.Border" : kStyledNames.find(name) != std::wstring_view::npos;
}

// Aplica o layout do conceito e o vidro do host em que o elemento estiver.
static void StyleElement(FrameworkElement const& element, std::wstring_view name, std::wstring_view type) {
    if (g_concept) ApplyStartLayout(element, name, g_startProcess);

    if (StyleSearch(element, name)) return;
    if (StyleStartMenu(element, name, type)) return;
    if (StyleQuickSettings(element, name, type)) return;
    StyleNotifications(element, name);
}

// Cada elemento novo: raiz (para o dump de descoberta, com MenusDump), flyouts do Iniciar (abrem ao lado da barra
// lateral) e os elementos a estilizar.
void OnElementAdded(ParentChildRelation const& relation, VisualElement const& element) {
    std::wstring_view name = element.Name ? element.Name : L"", type = element.Type ? element.Type : L"";

    if (!relation.Parent && Setting(L"MenusDump", 0)) {
        Log(L"raiz %s#%s", element.Type ? element.Type : L"", element.Name ? element.Name : L"");
        if (auto root = ElementFromHandle(element.Handle)) WatchTreeDump(root);
    }

    bool flyout = type == L"Windows.UI.Xaml.Controls.FlyoutPresenter" || type == L"Windows.UI.Xaml.Controls.MenuFlyoutPresenter";
    if (g_startProcess && g_concept && flyout)
        if (auto presenter = FrameworkElementFromHandle(element.Handle)) WatchFlyout(presenter);

    if (g_enabled && IsStyled(name, type))
        if (auto target = FrameworkElementFromHandle(element.Handle)) StyleElement(target, name, type);
}

// rundll32 TemaMenus.dll,Run: vigia o Iniciar, a pesquisa, as Configurações Rápidas (ShellHost, 24H2+) e o painel de
// notificações/relógio (ShellExperienceHost) e injeta em cada processo novo. A cada 1 s: o ShellExperienceHost acorda
// quando o painel abre, e quanto antes injetar, menos tempo o painel fica sem vidro.
extern "C" __declspec(dllexport) void CALLBACK Run(HWND, HINSTANCE, LPSTR, int) {
    InjectIntoProcesses(L"TemaMenusRun", {L"StartMenuExperienceHost.exe", L"SearchHost.exe", L"ShellHost.exe", L"ShellExperienceHost.exe"},
                        L"Menus", 1000);
}
