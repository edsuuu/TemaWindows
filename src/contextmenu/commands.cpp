#include "contextmenu/contextmenu.h"
#include "common/log.h"

#include <unknwn.h>
#include <winrt/base.h>
#include <shlwapi.h>
#include <mutex>

#pragma comment(linker, "/EXPORT:DllGetClassObject,PRIVATE")
#pragma comment(linker, "/EXPORT:DllCanUnloadNow,PRIVATE")

static const GUID kBaseClsid = {0x6f3b2a90, 0x1c4d, 0x4e8a, {0xb5, 0xf7, 0x3d, 0x9e, 0x0c, 0x2a, 0x71, 0x00}};
static const BYTE kBackgroundSlots = 0x00;
static const BYTE kItemSlots = 0x10;
static const BYTE kWsl = 0x20;
static const int kSlotCount = 10;

static std::recursive_mutex g_lock;
static Selection g_selection;
static std::vector<std::shared_ptr<LegacyItem>> g_items;
static std::wstring g_selectionKey;
static ULONGLONG g_collectedAt;

// Item do menu antigo na posição `slot` para a seleção. O menu novo pergunta título, ícone e estado de cada posição em
// seguida, então a lista é montada uma vez e reaproveitada por 2 s para a mesma seleção. Sem `items` (GetFlags,
// EnumSubCommands), vale a última lista montada.
static std::shared_ptr<LegacyItem> ItemAt(IShellItemArray* items, bool background, int slot) {
    std::lock_guard guard(g_lock);
    if (items) {
        Selection selection;
        if (!ReadSelection(items, background, selection)) {
            Log(L"seleção fora do sistema de arquivos (posição %d)", slot);
            g_items.clear();
            g_selectionKey.clear();
            return nullptr;
        }

        std::wstring key = background ? L"fundo" : L"itens";
        for (auto const& path : selection.paths) key += L"|" + path;
        if (key != g_selectionKey || GetTickCount64() - g_collectedAt > 2000) {
            g_items = SharedLegacyItems(selection, key);
            g_selection = selection;
            g_selectionKey = key;
            g_collectedAt = GetTickCount64();
        }
    }

    if (g_selection.background != background || slot >= int(g_items.size())) return nullptr;
    return g_items[slot];
}

// Seleção da última lista montada, para rodar o item clicado.
static Selection CurrentSelection() {
    std::lock_guard guard(g_lock);
    return g_selection;
}

// "Abrir WSL": Terminal do Windows com o perfil padrão (PowerShell) rodando o WSL já na home. O '~' vai entre aspas
// simples para o PowerShell não trocar pela home do Windows.
static std::shared_ptr<LegacyItem> WslItem() {
    wchar_t system[MAX_PATH], terminal[MAX_PATH];
    GetSystemDirectoryW(system, MAX_PATH);
    ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\Microsoft\\WindowsApps\\wt.exe", terminal, MAX_PATH);

    auto item = std::make_shared<LegacyItem>();
    item->title = L"Abrir WSL";
    item->icon = std::wstring(system) + L"\\wsl.exe";
    item->program = terminal;
    item->arguments = L"pwsh.exe -NoLogo -NoExit -Command \"wsl --cd '~'\"";
    return item;
}

struct LegacyCommand : winrt::implements<LegacyCommand, IExplorerCommand> {
    bool background;
    int slot;
    std::shared_ptr<LegacyItem> fixed;

    LegacyCommand(bool background, int slot, std::shared_ptr<LegacyItem> fixed) : background(background), slot(slot), fixed(std::move(fixed)) {}

    // O item desta posição: fixo (submenu, Abrir WSL) ou o da lista da seleção.
    std::shared_ptr<LegacyItem> Item(IShellItemArray* items) {
        return fixed ? fixed : ItemAt(items, background, slot);
    }

    IFACEMETHODIMP GetTitle(IShellItemArray* items, LPWSTR* name) override {
        auto item = Item(items);
        return item ? SHStrDupW(item->title.c_str(), name) : E_FAIL;
    }

    IFACEMETHODIMP GetIcon(IShellItemArray* items, LPWSTR* icon) override {
        auto item = Item(items);
        return item && !item->icon.empty() ? SHStrDupW(item->icon.c_str(), icon) : E_NOTIMPL;
    }

    IFACEMETHODIMP GetToolTip(IShellItemArray*, LPWSTR* tip) override {
        *tip = nullptr;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetCanonicalName(GUID* name) override {
        *name = GUID_NULL;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetState(IShellItemArray* items, BOOL, EXPCMDSTATE* state) override {
        *state = Item(items) ? ECS_ENABLED : ECS_HIDDEN;
        return S_OK;
    }

    IFACEMETHODIMP Invoke(IShellItemArray* items, IBindCtx*) override {
        auto item = Item(items);
        if (item && item->children.empty()) InvokeLegacyItem(CurrentSelection(), *item);
        return S_OK;
    }

    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) override {
        auto item = Item(nullptr);
        *flags = item && !item->children.empty() ? ECF_HASSUBCOMMANDS : ECF_DEFAULT;
        return S_OK;
    }

    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** commands) override;
};

struct CommandList : winrt::implements<CommandList, IEnumExplorerCommand> {
    std::vector<winrt::com_ptr<IExplorerCommand>> commands;
    size_t next = 0;

    IFACEMETHODIMP Next(ULONG count, IExplorerCommand** out, ULONG* fetched) override {
        ULONG done = 0;
        for (; done < count && next < commands.size(); ++done, ++next) commands[next].copy_to(out + done);
        if (fetched) *fetched = done;
        return done == count ? S_OK : S_FALSE;
    }

    IFACEMETHODIMP Skip(ULONG count) override {
        next += count;
        return next <= commands.size() ? S_OK : S_FALSE;
    }

    IFACEMETHODIMP Reset() override {
        next = 0;
        return S_OK;
    }

    IFACEMETHODIMP Clone(IEnumExplorerCommand** copy) override {
        *copy = nullptr;
        return E_NOTIMPL;
    }
};

// Filhos de um submenu (7-Zip, por exemplo), cada um como comando fixo.
IFACEMETHODIMP LegacyCommand::EnumSubCommands(IEnumExplorerCommand** commands) {
    *commands = nullptr;
    auto item = Item(nullptr);
    if (!item || item->children.empty()) return E_NOTIMPL;

    try {
        auto list = winrt::make_self<CommandList>();
        for (auto const& child : item->children) list->commands.push_back(winrt::make_self<LegacyCommand>(background, -1, child).as<IExplorerCommand>());
        list.as<IEnumExplorerCommand>().copy_to(commands);
        return S_OK;
    } catch (...) {
        return winrt::to_hresult();
    }
}

struct CommandFactory : winrt::implements<CommandFactory, IClassFactory> {
    BYTE code;

    explicit CommandFactory(BYTE code) : code(code) {}

    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID iid, void** object) override {
        *object = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;

        try {
            bool background = code < kItemSlots;
            auto command = code == kWsl ? winrt::make_self<LegacyCommand>(true, -1, WslItem()) : winrt::make_self<LegacyCommand>(background, code & 0x0F, nullptr);
            return command.as<IUnknown>()->QueryInterface(iid, object);
        } catch (...) {
            return winrt::to_hresult();
        }
    }

    IFACEMETHODIMP LockServer(BOOL) override {
        return S_OK;
    }
};

// Uma classe por posição do menu (o manifesto do pacote lista todas): {6F3B2A90-...-3D9E0C2A71xx}, com xx = 00-09
// fundo de pasta, 10-19 arquivos e pastas, 20 Abrir WSL.
STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** object) {
    *object = nullptr;
    BYTE code = clsid.Data4[7];
    bool known = memcmp(&clsid, &kBaseClsid, sizeof(GUID) - 1) == 0 &&
                 (code == kWsl || (code & 0xF0) <= kItemSlots && (code & 0x0F) < kSlotCount);
    if (!known) return CLASS_E_CLASSNOTAVAILABLE;

    try {
        return winrt::make_self<CommandFactory>(code).as<IUnknown>()->QueryInterface(iid, object);
    } catch (...) {
        return winrt::to_hresult();
    }
}

// Fica carregada até o Explorer reiniciar: a lista e as extensões criadas vivem na DLL.
STDAPI DllCanUnloadNow() {
    return S_FALSE;
}
