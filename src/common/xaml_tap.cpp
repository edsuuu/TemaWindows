#include "common/xaml_tap.h"
#include "common/log.h"
#include "common/paths.h"
#include "common/registry.h"

#include <ocidl.h>
#include <tlhelp32.h>
#include <algorithm>
#include <string>
#include <thread>
#include <unordered_map>

#pragma comment(linker, "/export:DllGetClassObject,PRIVATE")
#pragma comment(linker, "/export:DllCanUnloadNow,PRIVATE")

using InitializeXamlDiagnosticsExFn = HRESULT(WINAPI*)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, CLSID, LPCWSTR);

static winrt::com_ptr<IXamlDiagnostics> g_diagnostics;

// Recebe cada mudança da árvore XAML do processo e repassa os elementos novos ao componente. Exceção aqui derrubaria
// o processo hospedeiro (Explorer, Iniciar, painéis): fica só no log.
struct TreeWatcher : winrt::implements<TreeWatcher, IVisualTreeServiceCallback2, winrt::non_agile> {
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation relation, VisualElement element, VisualMutationType type) override {
        if (type != Add) return S_OK;

        try {
            OnElementAdded(relation, element);
        } catch (...) {
            Log(L"erro em OnVisualTreeChange: %08X", winrt::to_hresult());
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle, VisualElementState, LPCWSTR) override {
        return S_OK;
    }
};

// Começa a acompanhar a árvore. AdviseVisualTreeChange chamado da thread da UI trava o processo: vai numa thread à parte.
static void StartWatching() {
    auto watcher = winrt::make_self<TreeWatcher>();

    std::thread([watcher] {
        HRESULT hr = g_diagnostics.as<IVisualTreeService3>()->AdviseVisualTreeChange(watcher.get());
        Log(L"AdviseVisualTreeChange: %08X", hr);
    }).detach();
}

// O TAP de diagnóstico XAML que o processo carrega a pedido do InitializeXamlDiagnosticsEx; o site é o IXamlDiagnostics
// do processo. Só a primeira chamada inicia o componente.
struct Tap : winrt::implements<Tap, IObjectWithSite, winrt::non_agile> {
    winrt::com_ptr<IUnknown> site;

    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* newSite) override {
        static bool started;
        site.copy_from(newSite);
        if (!newSite || started) return S_OK;

        started = true;
        try {
            g_diagnostics = site.as<IXamlDiagnostics>();
            OnTapStarted();
            StartWatching();
        } catch (...) {
            Log(L"erro no SetSite: %08X", winrt::to_hresult());
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** result) override {
        return site ? site->QueryInterface(riid, result) : E_FAIL;
    }
};

// Fábrica COM do TAP.
struct TapFactory : winrt::implements<TapFactory, IClassFactory> {
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** result) override {
        return outer ? CLASS_E_NOAGGREGATION : winrt::make_self<Tap>()->QueryInterface(riid, result);
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override {
        return S_OK;
    }
};

// Entrada COM da DLL: só conhece o CLSID do TAP deste componente.
STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** result) {
    return clsid == kTapClsid ? winrt::make_self<TapFactory>()->QueryInterface(riid, result) : CLASS_E_CLASSNOTAVAILABLE;
}

// A DLL nunca descarrega: callbacks e visuais dela continuam vivos no processo.
STDAPI DllCanUnloadNow() {
    return S_FALSE;
}

// Objeto XAML por trás de um handle do diagnóstico.
winrt::Windows::Foundation::IInspectable ElementFromHandle(InstanceHandle handle) {
    winrt::Windows::Foundation::IInspectable object;
    winrt::check_hresult(g_diagnostics->GetIInspectableFromHandle(handle, reinterpret_cast<::IInspectable**>(winrt::put_abi(object))));
    return object;
}

// Elemento por trás de um handle do diagnóstico, se for um FrameworkElement.
FrameworkElement FrameworkElementFromHandle(InstanceHandle handle) {
    return ElementFromHandle(handle).try_as<FrameworkElement>();
}

// A injeção é a API de diagnóstico XAML do Windows.UI.Xaml.dll do sistema (a mesma que o TranslucentTB usa).
static InitializeXamlDiagnosticsExFn LoadInitializeXamlDiagnostics() {
    HMODULE xaml = LoadLibraryExW(L"Windows.UI.Xaml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return reinterpret_cast<InitializeXamlDiagnosticsExFn>(GetProcAddress(xaml, "InitializeXamlDiagnosticsEx"));
}

// Pede ao processo para carregar esta DLL como TAP, tentando as conexões VisualDiagConnection1 a 100.
HRESULT InjectTap(DWORD pid) {
    static const auto initialize = LoadInitializeXamlDiagnostics();
    static const std::wstring dll = ModulePath();
    HRESULT hr = E_FAIL;

    for (int i = 1; i <= 100; i++) {
        wchar_t connection[64];
        swprintf_s(connection, L"VisualDiagConnection%d", i);
        hr = initialize(connection, pid, L"", dll.c_str(), kTapClsid, nullptr);
        if (hr != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) break;
    }
    return hr;
}

// Injeta num processo-alvo se ainda não foi (attempts: 0 = novo, positivo = tentativas, -1 = injetado). NOT_FOUND é
// processo ainda carregando ou suspenso (o ShellExperienceHost dorme até o painel abrir): tenta sem limite e loga uma
// vez só; outros erros desistem depois de 30 tentativas.
static void TryInject(PROCESSENTRY32W const& process, int& attempts) {
    if (attempts < 0 || attempts >= 30) return;

    HRESULT hr = InjectTap(process.th32ProcessID);
    bool asleep = hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    if (SUCCEEDED(hr) || !asleep || attempts == 0) Log(L"injetando em %s %lu: %08X", process.szExeFile, process.th32ProcessID, hr);
    attempts = SUCCEEDED(hr) ? -1 : asleep ? std::max(attempts, 1) : attempts + 1;
}

// O executável é um dos alvos?
static bool IsTarget(const wchar_t* exe, std::initializer_list<const wchar_t*> exeNames) {
    return std::any_of(exeNames.begin(), exeNames.end(), [exe](const wchar_t* name) { return !_wcsicmp(exe, name); });
}

// Uma varredura dos processos: injeta nos alvos e esquece os que já morreram.
static void ScanProcesses(std::initializer_list<const wchar_t*> exeNames, std::unordered_map<DWORD, int>& attempts) {
    std::unordered_map<DWORD, int> alive;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W process{sizeof process};

    for (BOOL ok = Process32FirstW(snapshot, &process); ok; ok = Process32NextW(snapshot, &process)) {
        if (!IsTarget(process.szExeFile, exeNames)) continue;

        DWORD pid = process.th32ProcessID;
        int& count = alive[pid] = attempts.count(pid) ? attempts[pid] : 0;
        TryInject(process, count);
    }

    CloseHandle(snapshot);
    attempts.swap(alive);
}

// Vigia do componente (rundll32 <dll>,Run): injeta em cada processo novo com um dos nomes dados, enquanto o recurso
// estiver ligado no registro. O mutex garante um vigia só por componente.
void InjectIntoProcesses(const wchar_t* mutexName, std::initializer_list<const wchar_t*> exeNames, const wchar_t* enabledSetting,
                         DWORD intervalMs) {
    CreateMutexW(nullptr, FALSE, mutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) return;

    std::unordered_map<DWORD, int> attempts;
    for (;; Sleep(intervalMs))
        if (Enabled(enabledSetting)) ScanProcesses(exeNames, attempts);
}
