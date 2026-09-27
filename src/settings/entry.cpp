#include "settings/settings_glass.h"
#include "common/log.h"
#include "common/registry.h"
#include "common/xaml_tap.h"

#include <string_view>

const CLSID kTapClsid = {0x8e3b5f21, 0x7c4a, 0x4d9e, {0xb1, 0xf6, 0x2a, 0x7d, 0x93, 0xc0, 0xe5, 0xb8}};

static bool g_enabled;

// Lê o liga/desliga (HKCU\Software\TemaBarra\Janelas) quando a DLL entra no processo das Configurações.
void OnTapStarted() {
    g_enabled = Enabled(L"Janelas");
    Log(L"TAP carregado (Janelas=%d)", g_enabled);
}

// Só a página raiz das Configurações interessa.
void OnElementAdded(ParentChildRelation const&, VisualElement const& element) {
    if (!g_enabled || !element.Type || std::wstring_view(element.Type) != L"SystemSettings.View.RootPage") return;

    ApplySettingsGlass(ElementFromHandle(element.Handle).as<DependencyObject>());
}

// rundll32 TemaJanelas.dll,Run: vigia as Configurações e injeta em cada processo novo.
extern "C" __declspec(dllexport) void CALLBACK Run(HWND, HINSTANCE, LPSTR, int) {
    InjectIntoProcesses(L"TemaJanelasRun", {L"SystemSettings.exe"}, L"Janelas", 2000);
}
