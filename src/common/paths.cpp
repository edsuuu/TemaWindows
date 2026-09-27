#include "common/paths.h"

#include <windows.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

// Caminho completo do próprio módulo: a DLL ou o exe que contém este código.
std::wstring ModulePath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, MAX_PATH);
    return path;
}

// Nome do próprio módulo, sem pasta e sem extensão (ex.: "TemaBarra").
std::wstring ModuleName() {
    std::wstring path = ModulePath();
    std::wstring name = path.substr(path.rfind(L'\\') + 1);
    return name.substr(0, name.rfind(L'.'));
}

// Caminho dentro da raiz do projeto, que é a pasta acima da pasta do módulo (bin\ -> raiz).
std::wstring ProjectPath(std::wstring_view relative) {
    std::wstring root = ModulePath();
    root.erase(root.rfind(L'\\'));
    root.erase(root.rfind(L'\\') + 1);
    return root + std::wstring(relative);
}
