#include "contextmenu/contextmenu.h"
#include "common/log.h"
#include "common/paths.h"

#include <unknwn.h>
#include <winrt/base.h>
#include <ocidl.h>
#include <xamlom.h>
#include <tlhelp32.h>
#include <string>
#include <thread>

// {D4E1C0A7-5B2E-4F61-9C3A-7E8F2B6A1C90}
const CLSID kMenuTapClsid = {0xd4e1c0a7, 0x5b2e, 0x4f61, {0x9c, 0x3a, 0x7e, 0x8f, 0x2b, 0x6a, 0x1c, 0x90}};

static winrt::com_ptr<IXamlDiagnostics> g_diagnostics;

// Libera o que o GetPropertyValuesChain devolve.
static void FreeChain(unsigned sourceCount, PropertyChainSource* sources, unsigned propertyCount, PropertyChainValue* values) {
    for (unsigned i = 0; i < sourceCount; ++i) {
        SysFreeString(sources[i].TargetType);
        SysFreeString(sources[i].Name);
        SysFreeString(sources[i].SrcInfo.FileName);
        SysFreeString(sources[i].SrcInfo.Hash);
    }
    for (unsigned i = 0; i < propertyCount; ++i) {
        SysFreeString(values[i].Type);
        SysFreeString(values[i].DeclaringType);
        SysFreeString(values[i].ValueType);
        SysFreeString(values[i].ItemType);
        SysFreeString(values[i].Value);
        SysFreeString(values[i].PropertyName);
    }
    CoTaskMemFree(sources);
    CoTaskMemFree(values);
}

// Esconde um elemento (Visibility = Collapsed) pela API de diagnóstico: o menu é WinUI 3, sem projeção no SDK do
// Windows. O índice da propriedade sai da cadeia de propriedades do próprio elemento.
static void Collapse(InstanceHandle handle) {
    auto service = g_diagnostics.as<IVisualTreeService>();
    unsigned sourceCount = 0, propertyCount = 0, visibility = UINT_MAX;
    PropertyChainSource* sources = nullptr;
    PropertyChainValue* values = nullptr;
    HRESULT hr = service->GetPropertyValuesChain(handle, &sourceCount, &sources, &propertyCount, &values);
    if (SUCCEEDED(hr)) {
        for (unsigned i = 0; i < propertyCount && visibility == UINT_MAX; ++i)
            if (values[i].PropertyName && !wcscmp(values[i].PropertyName, L"Visibility")) visibility = values[i].Index;
        FreeChain(sourceCount, sources, propertyCount, values);
    }

    BSTR typeName = SysAllocString(L"Microsoft.UI.Xaml.Visibility"), collapsed = SysAllocString(L"Collapsed");
    InstanceHandle value = 0;
    if (SUCCEEDED(hr) && visibility != UINT_MAX) hr = service->CreateInstance(typeName, collapsed, &value);
    if (SUCCEEDED(hr) && value) hr = service->SetProperty(handle, value, visibility);
    SysFreeString(typeName);
    SysFreeString(collapsed);
    if (FAILED(hr) || visibility == UINT_MAX) Log(L"não escondi o elemento %llx: %08X", handle, hr);
}

// Cada elemento novo do WinUI 3 do Explorer. O menu novo é um CommandBarFlyout com os itens num StackPanel; o "Mostrar
// mais opções" é o AppBarButton "expandtoclassic" e chega depois do separador logo acima dele, que some junto.
struct MenuWatcher : winrt::implements<MenuWatcher, IVisualTreeServiceCallback2, winrt::non_agile> {
    ParentChildRelation separator{};

    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation relation, VisualElement element, VisualMutationType type) override {
        if (type != Add || !element.Type) return S_OK;

        try {
            if (!wcscmp(element.Type, L"Microsoft.UI.Xaml.Controls.AppBarSeparator")) {
                separator = relation;
            } else if (element.Name && !wcscmp(element.Name, L"expandtoclassic")) {
                Collapse(element.Handle);
                if (separator.Parent == relation.Parent && separator.ChildIndex + 1 == relation.ChildIndex) Collapse(separator.Child);
            }
        } catch (...) {
            Log(L"erro em OnVisualTreeChange: %08X", winrt::to_hresult());
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle, VisualElementState, LPCWSTR) override {
        return S_OK;
    }
};

// TAP que o WinUI 3 do Explorer carrega a pedido do InitializeXamlDiagnosticsEx; o site é o IXamlDiagnostics dele.
// AdviseVisualTreeChange chamado da thread da UI trava o processo: vai numa thread à parte.
struct MenuTap : winrt::implements<MenuTap, IObjectWithSite, winrt::non_agile> {
    winrt::com_ptr<IUnknown> site;

    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* newSite) override {
        site.copy_from(newSite);
        if (!newSite || g_diagnostics) return S_OK;

        try {
            g_diagnostics = site.as<IXamlDiagnostics>();
            auto watcher = winrt::make_self<MenuWatcher>();
            std::thread([watcher] {
                Log(L"AdviseVisualTreeChange: %08X", g_diagnostics.as<IVisualTreeService3>()->AdviseVisualTreeChange(watcher.get()));
            }).detach();
        } catch (...) {
            Log(L"erro no SetSite: %08X", winrt::to_hresult());
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetSite(REFIID iid, void** object) override {
        return site ? site->QueryInterface(iid, object) : E_FAIL;
    }
};

struct MenuTapFactory : winrt::implements<MenuTapFactory, IClassFactory> {
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** object) override {
        return outer ? CLASS_E_NOAGGREGATION : winrt::make_self<MenuTap>()->QueryInterface(iid, object);
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override {
        return S_OK;
    }
};

HRESULT GetMenuTapFactory(REFIID iid, void** object) {
    return winrt::make_self<MenuTapFactory>()->QueryInterface(iid, object);
}

// Injeta o TAP no Explorer dono da barra, quando ele já tem o WinUI 3 (o menu novo é WinUI 3) e ainda não tem esta DLL.
// A API de injeção é a do Microsoft.Internal.FrameworkUdk.dll que o próprio Explorer carregou.
void InjectMenuTap() {
    static DWORD failedPid;
    static int failures;
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    DWORD pid = 0;
    if (!tray || !GetWindowThreadProcessId(tray, &pid) || (pid == failedPid && failures >= 3)) return;

    std::wstring self = ModulePath(), udk;
    bool injected = false;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32W module{sizeof module};
    for (BOOL ok = Module32FirstW(snapshot, &module); ok; ok = Module32NextW(snapshot, &module)) {
        if (!_wcsicmp(module.szExePath, self.c_str())) injected = true;
        if (!_wcsicmp(module.szModule, L"Microsoft.Internal.FrameworkUdk.dll")) udk = module.szExePath;
    }
    CloseHandle(snapshot);
    if (injected || udk.empty()) return;

    using Initialize = HRESULT(WINAPI*)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, CLSID, LPCWSTR);
    auto initialize = reinterpret_cast<Initialize>(GetProcAddress(LoadLibraryExW(udk.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH), "InitializeXamlDiagnosticsEx"));
    HRESULT hr = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    for (int i = 1; initialize && i <= 100; i++) {
        wchar_t connection[64];
        swprintf_s(connection, L"WinUIVisualDiagConnection%d", i);
        hr = initialize(connection, pid, L"", self.c_str(), kMenuTapClsid, nullptr);
        if (hr != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) break;
    }
    Log(L"TAP do menu no explorer %lu: %08X", pid, hr);
    if (FAILED(hr)) failures = pid == failedPid ? failures + 1 : 1, failedPid = pid;
}
