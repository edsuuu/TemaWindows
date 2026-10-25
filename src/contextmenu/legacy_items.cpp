#include "contextmenu/contextmenu.h"
#include "common/log.h"

#include <unknwn.h>
#include <winrt/base.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <appmodel.h>
#include <map>
#include <mutex>
#include <set>

static const UINT kFirstId = 1;
static const UINT kLastId = 0x7FFF;

// Texto de um valor em HKCR (variáveis de ambiente já expandidas); vazio quando não existe. value nulo = padrão.
static std::wstring ClassText(std::wstring const& key, const wchar_t* value) {
    wchar_t text[1024];
    DWORD size = sizeof text;

    if (RegGetValueW(HKEY_CLASSES_ROOT, key.c_str(), value, RRF_RT_REG_SZ, nullptr, text, &size) != ERROR_SUCCESS) return L"";
    return text;
}

// Se o valor existe em HKCR, de qualquer tipo.
static bool HasValue(std::wstring const& key, const wchar_t* value) {
    return RegGetValueW(HKEY_CLASSES_ROOT, key.c_str(), value, RRF_RT_ANY, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

// Nomes das subchaves de uma chave do HKCR.
static std::vector<std::wstring> SubKeys(std::wstring const& key) {
    std::vector<std::wstring> names;
    HKEY handle;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, key.c_str(), 0, KEY_READ, &handle) != ERROR_SUCCESS) return names;

    wchar_t name[256];
    for (DWORD i = 0, size = 256; RegEnumKeyExW(handle, i, name, &size, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS; ++i, size = 256) {
        names.push_back(name);
    }
    RegCloseKey(handle);
    return names;
}

// Minúsculas, para comparar verbos e CLSIDs.
static std::wstring Lower(std::wstring text) {
    CharLowerBuffW(text.data(), DWORD(text.size()));
    return text;
}

// Exe de uma linha de comando ("C:\x\app.exe" "%1" -> C:\x\app.exe), com a pasta achada pelo PATH quando falta.
static std::wstring CommandFile(std::wstring const& command) {
    std::wstring file = command[0] == L'"' ? command.substr(1, command.find(L'"', 1) - 1) : command.substr(0, command.find(L' '));
    if (file.empty() || file.find(L'\\') != std::wstring::npos) return file;

    wchar_t found[MAX_PATH];
    return SearchPathW(nullptr, file.c_str(), nullptr, MAX_PATH, found, nullptr) ? found : file;
}

// Arquivo que implementa uma classe COM (InprocServer32 ou, se não tiver, o exe do LocalServer32).
static std::wstring ServerFile(std::wstring const& clsid) {
    std::wstring file = ClassText(L"CLSID\\" + clsid + L"\\InprocServer32", nullptr);
    if (file.empty()) file = ClassText(L"CLSID\\" + clsid + L"\\LocalServer32", nullptr);
    return file.empty() ? file : CommandFile(file);
}

// Arquivo que executa um verbo do registro: o exe do comando ou o servidor do DelegateExecute/ExplorerCommandHandler.
static std::wstring VerbFile(std::wstring const& key) {
    std::wstring clsid = ClassText(key, L"ExplorerCommandHandler");
    if (clsid.empty()) clsid = ClassText(key + L"\\command", L"DelegateExecute");
    if (clsid.empty()) clsid = ClassText(key, L"DelegateExecute");
    if (!clsid.empty()) return ServerFile(clsid);

    std::wstring command = ClassText(key + L"\\command", nullptr);
    return command.empty() ? command : CommandFile(command);
}

// Arquivo do próprio Windows: dentro da pasta do Windows e assinado como Microsoft no CompanyName. A DLL da NVIDIA fica
// no DriverStore, dentro do Windows, mas é dela, então passa.
static bool IsWindowsFile(std::wstring const& path) {
    static std::mutex lock;
    static std::map<std::wstring, bool> cache;
    std::lock_guard guard(lock);
    auto cached = cache.find(Lower(path));
    if (cached != cache.end()) return cached->second;

    wchar_t windows[MAX_PATH];
    UINT length = GetWindowsDirectoryW(windows, MAX_PATH);
    bool result = _wcsnicmp(path.c_str(), windows, length) == 0;
    DWORD size = result ? GetFileVersionInfoSizeW(path.c_str(), nullptr) : 0;
    if (size) {
        std::vector<BYTE> data(size);
        struct { WORD language, codePage; }* translation = nullptr;
        wchar_t* company = nullptr;
        UINT bytes = 0;
        wchar_t query[64];
        if (GetFileVersionInfoW(path.c_str(), 0, size, data.data()) &&
            VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translation), &bytes) && bytes >= 4) {
            swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\CompanyName", translation->language, translation->codePage);
            if (VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&company), &bytes)) result = wcsstr(company, L"Microsoft") != nullptr;
        }
    }
    cache[Lower(path)] = result;
    return result;
}

// Texto do item como o menu novo mostra: indireto (@dll,-id) resolvido, sem o & do atalho e sem o que vem depois do tab.
static std::wstring CleanTitle(std::wstring text) {
    if (text[0] == L'@') {
        wchar_t resolved[256];
        if (SUCCEEDED(SHLoadIndirectString(text.c_str(), resolved, 256, nullptr))) text = resolved;
    }

    std::wstring title;
    for (size_t i = 0; i < text.size() && text[i] != L'\t'; ++i) {
        if (text[i] == L'&' && i + 1 < text.size()) ++i;
        title += text[i];
    }
    return title;
}

// Classes do registro de onde o Explorer tira o menu para essa seleção: fundo de pasta, pasta ou arquivo (pelo tipo do
// primeiro item, com o ProgID que o usuário escolheu).
static std::vector<std::wstring> ClassKeys(Selection const& selection) {
    if (selection.background) return {L"Directory\\Background"};

    std::wstring const& path = selection.paths[0];
    if (GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_DIRECTORY) return {L"Directory", L"Folder", L"AllFilesystemObjects"};

    std::vector<std::wstring> keys;
    std::wstring extension = PathFindExtensionW(path.c_str());
    if (!extension.empty()) {
        wchar_t progId[256];
        DWORD size = 256;
        if (SUCCEEDED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_PROGID, extension.c_str(), nullptr, progId, &size))) keys.push_back(progId);
        keys.push_back(extension);
        keys.push_back(L"SystemFileAssociations\\" + extension);
        std::wstring perceived = ClassText(extension, L"PerceivedType");
        if (!perceived.empty()) keys.push_back(L"SystemFileAssociations\\" + perceived);
    }
    keys.push_back(L"*");
    keys.push_back(L"AllFilesystemObjects");
    return keys;
}

// Verbos de terceiros em <classe>\shell. Pula os escondidos (só com Shift, desativados, só para programas), os em cascata
// (sem comando), os com AppliesTo e os que o menu novo já tem (abrir, imprimir, o verbo padrão da classe).
static void AddVerbs(std::wstring const& classKey, std::vector<std::shared_ptr<LegacyItem>>& items, std::set<std::wstring>& seen) {
    static const std::set<std::wstring> builtIn = {L"open", L"opennew", L"openas", L"runas", L"runasuser", L"explore", L"find", L"print", L"printto"};
    std::wstring defaultVerb = Lower(ClassText(classKey + L"\\shell", nullptr));

    for (auto const& verb : SubKeys(classKey + L"\\shell")) {
        std::wstring key = classKey + L"\\shell\\" + verb, name = Lower(verb);
        if (seen.count(name) || builtIn.count(name) || name == defaultVerb) continue;
        if (HasValue(key, L"Extended") || HasValue(key, L"LegacyDisable") || HasValue(key, L"ProgrammaticAccessOnly") ||
            HasValue(key, L"AppliesTo") || HasValue(key, L"OnlyInBrowserWindow")) continue;

        std::wstring file = VerbFile(key);
        if (file.empty() || IsWindowsFile(file)) continue;

        auto item = std::make_shared<LegacyItem>();
        std::wstring title = ClassText(key, L"MUIVerb");
        if (title.empty()) title = ClassText(key, nullptr);
        item->title = CleanTitle(title.empty() ? verb : title);
        item->icon = ClassText(key, L"Icon");
        if (item->icon.empty() && Lower(file).ends_with(L".exe")) item->icon = file;
        std::erase(item->icon, L'"');
        item->verb = verb;
        items.push_back(item);
        seen.insert(name);
    }
}

// IShellItemArray com os caminhos da seleção.
static winrt::com_ptr<IShellItemArray> SelectionArray(std::vector<std::wstring> const& paths) {
    std::vector<PIDLIST_ABSOLUTE> ids;
    for (auto const& path : paths) {
        PIDLIST_ABSOLUTE id;
        if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &id, 0, nullptr))) ids.push_back(id);
    }

    winrt::com_ptr<IShellItemArray> array;
    if (!ids.empty()) SHCreateShellItemArrayFromIDLists(UINT(ids.size()), const_cast<PCIDLIST_ABSOLUTE_ARRAY>(ids.data()), array.put());
    for (auto id : ids) CoTaskMemFree(id);
    return array;
}

// Pasta onde o comando roda: a própria pasta no fundo, a pasta do primeiro item numa seleção.
static std::wstring SelectionFolder(Selection const& selection) {
    std::wstring folder = selection.paths[0];
    size_t slash = folder.find_last_of(L'\\');
    if (!selection.background && slash != std::wstring::npos) folder.erase(slash == 2 ? 3 : slash);
    return folder;
}

// Se é o fundo da própria área de trabalho.
static bool IsDesktop(Selection const& selection) {
    PWSTR desktop = nullptr;
    bool result = selection.background && SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop)) &&
                  _wcsicmp(desktop, selection.paths[0].c_str()) == 0;
    CoTaskMemFree(desktop);
    return result;
}

// Pasta como o Explorer passa às extensões: no fundo da área de trabalho é a raiz do shell, no resto é o caminho.
static PIDLIST_ABSOLUTE FolderId(Selection const& selection) {
    PIDLIST_ABSOLUTE id = nullptr;
    if (IsDesktop(selection)) SHGetFolderLocation(nullptr, CSIDL_DESKTOP, nullptr, 0, &id);
    else SHParseDisplayName(SelectionFolder(selection).c_str(), nullptr, &id, 0, nullptr);
    return id;
}

// Pasta de instalação de um app da Store pela família do pacote; vazio se não estiver instalado.
static std::wstring PackagePath(const wchar_t* family) {
    UINT32 count = 0, length = 0;
    if (GetPackagesByPackageFamily(family, &count, nullptr, &length, nullptr) != ERROR_INSUFFICIENT_BUFFER || !count) return L"";

    std::vector<PWSTR> names(count);
    std::vector<wchar_t> buffer(length);
    wchar_t path[MAX_PATH];
    UINT32 pathLength = MAX_PATH;
    if (GetPackagesByPackageFamily(family, &count, names.data(), &length, buffer.data()) != ERROR_SUCCESS ||
        GetPackagePathByFullName(names[0], &pathLength, path) != ERROR_SUCCESS) return L"";
    return path;
}

// A extensão da NVIDIA só carrega dentro do explorer.exe (no dllhost do pacote responde CLASS_E_CLASSNOTAVAILABLE),
// então o item dela, que só aparece no fundo da área de trabalho, vira o app do Painel de controle direto.
static void AddNvidiaItem(Selection const& selection, std::vector<std::shared_ptr<LegacyItem>>& items) {
    const wchar_t* family = L"NVIDIACorp.NVIDIAControlPanel_56jybvy8sckqj";
    std::wstring folder = PackagePath(family);
    if (!IsDesktop(selection) || folder.empty()) return;

    auto item = std::make_shared<LegacyItem>();
    item->title = L"Painel de controle da NVIDIA";
    item->icon = folder + L"\\nvcplui.exe";
    item->program = std::wstring(L"shell:AppsFolder\\") + family + L"!NVIDIACorp.NVIDIAControlPanel";
    items.push_back(item);
}

// Cria a extensão de menu como o Explorer faz (IShellExtInit com a pasta, os itens e a chave da classe) e pede o menu
// dela dentro de `menu`.
static winrt::com_ptr<IContextMenu> CreateHandlerMenu(Selection const& selection, CLSID const& clsid, std::wstring const& classKey, HMENU menu) {
    winrt::com_ptr<IShellExtInit> init;
    HRESULT created = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(init.put()));
    if (FAILED(created)) {
        Log(L"extensão em %s não foi criada: %08x", classKey.c_str(), created);
        return nullptr;
    }

    winrt::com_ptr<IDataObject> data;
    if (!selection.background) {
        auto array = SelectionArray(selection.paths);
        if (!array || FAILED(array->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(data.put())))) return nullptr;
    }

    HKEY key = nullptr;
    PIDLIST_ABSOLUTE folder = FolderId(selection);
    RegOpenKeyExW(HKEY_CLASSES_ROOT, classKey.c_str(), 0, KEY_READ, &key);
    HRESULT result = init->Initialize(folder, data.get(), key);
    if (key) RegCloseKey(key);
    CoTaskMemFree(folder);

    auto contextMenu = init.try_as<IContextMenu>();
    if (SUCCEEDED(result) && contextMenu) result = contextMenu->QueryContextMenu(menu, 0, kFirstId, kLastId, CMF_NORMAL);
    if (FAILED(result) || !contextMenu) {
        wchar_t clsidText[40];
        StringFromGUID2(clsid, clsidText, 40);
        Log(L"extensão %s em %s não montou o menu: %08x", clsidText, classKey.c_str(), result);
        return nullptr;
    }
    return contextMenu;
}

// Submenu que a extensão só preenche ao abrir: manda o WM_INITMENUPOPUP que o Explorer mandaria.
static void InitPopup(winrt::com_ptr<IContextMenu> const& contextMenu, HMENU submenu, int index) {
    if (auto menu3 = contextMenu.try_as<IContextMenu3>()) {
        LRESULT ignored;
        menu3->HandleMenuMsg2(WM_INITMENUPOPUP, WPARAM(submenu), index, &ignored);
    } else if (auto menu2 = contextMenu.try_as<IContextMenu2>()) {
        menu2->HandleMenuMsg(WM_INITMENUPOPUP, WPARAM(submenu), index);
    }
}

// Texto de um item de HMENU pela posição; vazio para separador, desativado ou desenhado pela extensão.
static std::wstring MenuText(HMENU menu, int index, MENUITEMINFOW& info) {
    wchar_t text[256] = {};
    info = {sizeof info};
    info.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_SUBMENU | MIIM_STRING | MIIM_BITMAP | MIIM_ID;
    info.dwTypeData = text;
    info.cch = 256;
    if (!GetMenuItemInfoW(menu, index, TRUE, &info) || (info.fType & MFT_SEPARATOR) || (info.fState & MFS_DISABLED)) return L"";
    return text[0] ? CleanTitle(text) : L"";
}

// Copia os itens que uma extensão pôs no HMENU, com ícone e submenus.
static void ReadMenu(winrt::com_ptr<IContextMenu> const& contextMenu, HMENU menu, LegacyItem const& source, std::vector<std::shared_ptr<LegacyItem>>& out) {
    int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; ++i) {
        MENUITEMINFOW info;
        std::wstring title = MenuText(menu, i, info);
        if (title.empty()) continue;

        auto item = std::make_shared<LegacyItem>();
        item->title = title;
        item->handler = source.handler;
        item->handlerKey = source.handlerKey;
        item->position = source.position;
        item->position.push_back(i);
        if (reinterpret_cast<INT_PTR>(info.hbmpItem) > reinterpret_cast<INT_PTR>(HBMMENU_POPUP_MINIMIZE)) item->icon = SaveMenuIcon(info.hbmpItem);
        if (info.hSubMenu) {
            InitPopup(contextMenu, info.hSubMenu, i);
            ReadMenu(contextMenu, info.hSubMenu, *item, item->children);
            if (item->children.empty()) continue;
        }
        out.push_back(item);
    }
}

// Extensão bloqueada pelo administrador (Shell Extensions\Blocked), que o Explorer não carrega.
static bool IsBlocked(std::wstring const& clsid) {
    const wchar_t* key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Blocked";
    return RegGetValueW(HKEY_LOCAL_MACHINE, key, clsid.c_str(), RRF_RT_ANY, nullptr, nullptr, nullptr) == ERROR_SUCCESS ||
           RegGetValueW(HKEY_CURRENT_USER, key, clsid.c_str(), RRF_RT_ANY, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

// Extensões de menu de terceiros em <classe>\shellex\ContextMenuHandlers: cria cada uma e copia os itens que ela põe.
static void AddHandlers(Selection const& selection, std::wstring const& classKey, std::vector<std::shared_ptr<LegacyItem>>& items, std::set<std::wstring>& seen) {
    std::wstring handlers = classKey + L"\\shellex\\ContextMenuHandlers";
    for (auto const& name : SubKeys(handlers)) {
        std::wstring clsidText = name[0] == L'{' ? name : ClassText(handlers + L"\\" + name, nullptr);
        CLSID clsid;
        if (seen.count(Lower(clsidText)) || FAILED(CLSIDFromString(clsidText.c_str(), &clsid)) || IsBlocked(clsidText)) continue;
        seen.insert(Lower(clsidText));
        if (Lower(clsidText) == L"{3d1975af-48c6-4f8e-a182-be0e08fa86a9}") {
            AddNvidiaItem(selection, items);
            continue;
        }

        std::wstring file = ServerFile(clsidText);
        if (file.empty() || IsWindowsFile(file)) continue;

        HMENU menu = CreatePopupMenu();
        LegacyItem source;
        source.handler = clsid;
        source.handlerKey = classKey;
        if (auto contextMenu = CreateHandlerMenu(selection, clsid, classKey, menu)) ReadMenu(contextMenu, menu, source, items);
        DestroyMenu(menu);
    }
}

// Caminhos da seleção. Só vale para itens do sistema de arquivos (Este Computador, Lixeira etc. ficam de fora).
bool ReadSelection(IShellItemArray* items, bool background, Selection& selection) {
    DWORD count = 0;
    if (!items || FAILED(items->GetCount(&count)) || !count) return false;

    selection.background = background;
    selection.paths.clear();
    for (DWORD i = 0; i < count; ++i) {
        winrt::com_ptr<IShellItem> item;
        PWSTR path = nullptr;
        if (FAILED(items->GetItemAt(i, item.put())) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return false;
        selection.paths.push_back(path);
        CoTaskMemFree(path);
    }
    return true;
}

// Itens que só aparecem em "Mostrar mais opções" para essa seleção: primeiro os verbos do registro, depois as extensões.
std::vector<std::shared_ptr<LegacyItem>> CollectLegacyItems(Selection const& selection) {
    std::vector<std::shared_ptr<LegacyItem>> items;
    std::set<std::wstring> seen;
    auto keys = ClassKeys(selection);

    for (auto const& key : keys) AddVerbs(key, items, seen);
    for (auto const& key : keys) AddHandlers(selection, key, items, seen);
    return items;
}

// Manda o comando para o IContextMenu, por verbo (texto) ou por id, com a pasta da seleção como diretório.
static void RunCommand(winrt::com_ptr<IContextMenu> const& contextMenu, std::wstring const& verb, UINT offset, Selection const& selection) {
    std::string verbAnsi;
    for (wchar_t c : verb) verbAnsi += char(c);
    std::wstring folder = SelectionFolder(selection);
    CMINVOKECOMMANDINFOEX info{sizeof info};
    info.fMask = CMIC_MASK_UNICODE;
    info.hwnd = GetForegroundWindow();
    info.lpVerb = verb.empty() ? MAKEINTRESOURCEA(offset) : verbAnsi.c_str();
    info.lpVerbW = verb.empty() ? MAKEINTRESOURCEW(offset) : verb.c_str();
    info.lpDirectoryW = folder.c_str();
    info.nShow = SW_SHOWNORMAL;

    HRESULT result = contextMenu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info));
    if (FAILED(result)) Log(L"InvokeCommand %s falhou: %08x", verb.empty() ? L"(id)" : verb.c_str(), result);
}

// Verbo do registro: roda pelo menu do próprio Explorer para a seleção (o mesmo do "Mostrar mais opções"), que resolve
// %V, %1, DelegateExecute etc.
static void InvokeVerb(Selection const& selection, LegacyItem const& item) {
    winrt::com_ptr<IContextMenu> contextMenu;
    if (selection.background) {
        winrt::com_ptr<IShellItem> folder;
        winrt::com_ptr<IShellFolder> shellFolder;
        if (SUCCEEDED(SHCreateItemFromParsingName(selection.paths[0].c_str(), nullptr, IID_PPV_ARGS(folder.put()))) &&
            SUCCEEDED(folder->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(shellFolder.put())))) {
            shellFolder->CreateViewObject(GetForegroundWindow(), IID_PPV_ARGS(contextMenu.put()));
        }
    } else if (auto array = SelectionArray(selection.paths)) {
        array->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(contextMenu.put()));
    }

    HMENU menu = CreatePopupMenu();
    if (contextMenu && SUCCEEDED(contextMenu->QueryContextMenu(menu, 0, kFirstId, kLastId, CMF_NORMAL))) RunCommand(contextMenu, item.verb, 0, selection);
    else Log(L"menu do Explorer indisponível para o verbo %s", item.verb.c_str());
    DestroyMenu(menu);
}

// Item de extensão: monta o menu dela de novo nesta thread, segue a posição até o item e confere o texto antes de clicar.
static void InvokeHandlerItem(Selection const& selection, LegacyItem const& item) {
    HMENU menu = CreatePopupMenu();
    auto contextMenu = CreateHandlerMenu(selection, item.handler, item.handlerKey, menu);
    HMENU current = contextMenu ? menu : nullptr;
    for (size_t i = 0; current && i + 1 < item.position.size(); ++i) {
        HMENU submenu = GetSubMenu(current, item.position[i]);
        if (submenu) InitPopup(contextMenu, submenu, item.position[i]);
        current = submenu;
    }

    MENUITEMINFOW info;
    if (current && MenuText(current, item.position.back(), info) == item.title && info.wID >= kFirstId) RunCommand(contextMenu, L"", info.wID - kFirstId, selection);
    else Log(L"item \"%s\" sumiu do menu da extensão", item.title.c_str());
    DestroyMenu(menu);
}

// Clique num item: programa próprio (Abrir WSL), verbo do registro ou item de extensão.
void InvokeLegacyItem(Selection const& selection, LegacyItem const& item) {
    if (!item.program.empty()) ShellExecuteW(nullptr, nullptr, item.program.c_str(), item.arguments.c_str(), nullptr, SW_SHOWNORMAL);
    else if (!item.verb.empty()) InvokeVerb(selection, item);
    else InvokeHandlerItem(selection, item);
}
