#include "wallpaper/folder_icons.h"
#include "common/paths.h"

#include <windows.h>
#include <shlobj.h>
#include <string>
#include <thread>

// Põe o ícone com o degradê (assets\folder.ico) numa pasta que ainda não tem desktop.ini: o arquivo oculto e de
// sistema com o IconResource e a pasta só leitura, que é o que faz o Explorer ler o arquivo. Pula repositórios git,
// para o desktop.ini não aparecer como arquivo novo.
static void SetFolderIcon(std::wstring const& folder, std::wstring const& icon) {
    std::wstring ini = folder + L"\\desktop.ini";
    if (GetFileAttributesW(ini.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    if (GetFileAttributesW((folder + L"\\.git").c_str()) != INVALID_FILE_ATTRIBUTES) return;

    std::wstring text = L"\xFEFF[.ShellClassInfo]\r\nIconResource=" + icon + L",0\r\n";
    HANDLE file = CreateFileW(ini.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    DWORD written;
    WriteFile(file, text.data(), DWORD(text.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(file);
    SetFileAttributesW(folder.c_str(), GetFileAttributesW(folder.c_str()) | FILE_ATTRIBUTE_READONLY);
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, folder.c_str(), nullptr);
}

// Todas as pastas da área de trabalho que ainda não têm ícone próprio.
static void SetDesktopFolderIcons(std::wstring const& desktop, std::wstring const& icon) {
    WIN32_FIND_DATAW entry;
    HANDLE find = FindFirstFileW((desktop + L"\\*").c_str(), &entry);
    if (find == INVALID_HANDLE_VALUE) return;

    do {
        bool folder = entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
        if (folder && wcscmp(entry.cFileName, L".") && wcscmp(entry.cFileName, L"..")) SetFolderIcon(desktop + L"\\" + entry.cFileName, icon);
    } while (FindNextFileW(find, &entry));
    FindClose(find);
}

// Aplica agora e toda vez que uma pasta é criada ou renomeada na área de trabalho do usuário.
static void FolderIconLoop() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &path))) return;
    std::wstring desktop = path, icon = ProjectPath(L"assets\\folder.ico");
    CoTaskMemFree(path);

    HANDLE change = FindFirstChangeNotificationW(desktop.c_str(), FALSE, FILE_NOTIFY_CHANGE_DIR_NAME);
    SetDesktopFolderIcons(desktop, icon);
    if (change == INVALID_HANDLE_VALUE) return;

    while (WaitForSingleObject(change, INFINITE) == WAIT_OBJECT_0) {
        SetDesktopFolderIcons(desktop, icon);
        if (!FindNextChangeNotification(change)) break;
    }
    FindCloseChangeNotification(change);
}

// Liga o vigia das pastas da área de trabalho numa thread própria.
void StartFolderIconService() {
    std::thread(FolderIconLoop).detach();
}
