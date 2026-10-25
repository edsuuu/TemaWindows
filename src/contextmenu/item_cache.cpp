#include "contextmenu/contextmenu.h"
#include "common/log.h"
#include "common/paths.h"

#include <cstdio>
#include <sstream>

// Uma linha por item: profundidade, texto, ícone, verbo, CLSID da extensão, classe dela, posição no menu dela, programa
// e argumentos.
static void WriteItems(std::wstring& text, std::vector<std::shared_ptr<LegacyItem>> const& items, int depth) {
    for (auto const& item : items) {
        wchar_t clsid[40] = L"";
        if (item->handler != CLSID{}) StringFromGUID2(item->handler, clsid, 40);
        std::wstring position;
        for (int index : item->position) position += std::to_wstring(index) + L",";

        text += std::to_wstring(depth) + L"\t" + item->title + L"\t" + item->icon + L"\t" + item->verb + L"\t" + clsid + L"\t" +
                item->handlerKey + L"\t" + position + L"\t" + item->program + L"\t" + item->arguments + L"\n";
        WriteItems(text, item->children, depth + 1);
    }
}

// Lê de volta as linhas de WriteItems, montando os submenus pela profundidade.
static std::vector<std::shared_ptr<LegacyItem>> ParseItems(std::wstring const& text) {
    std::vector<std::shared_ptr<LegacyItem>> items;
    std::vector<std::vector<std::shared_ptr<LegacyItem>>*> levels = {&items};
    std::wistringstream lines(text);

    for (std::wstring line; std::getline(lines, line);) {
        std::vector<std::wstring> fields;
        std::wistringstream parts(line);
        for (std::wstring field; std::getline(parts, field, L'\t');) fields.push_back(field);
        fields.resize(9);
        size_t depth = _wtoi(fields[0].c_str());
        if (depth >= levels.size()) continue;

        auto item = std::make_shared<LegacyItem>();
        item->title = fields[1];
        item->icon = fields[2];
        item->verb = fields[3];
        if (!fields[4].empty()) CLSIDFromString(fields[4].c_str(), &item->handler);
        item->handlerKey = fields[5];
        std::wistringstream position(fields[6]);
        for (std::wstring index; std::getline(position, index, L',');) item->position.push_back(_wtoi(index.c_str()));
        item->program = fields[7];
        item->arguments = fields[8];

        levels.resize(depth + 1);
        levels[depth]->push_back(item);
        levels.push_back(&item->children);
    }
    return items;
}

// Conteúdo de um arquivo UTF-16 sem BOM; vazio se não existir.
static std::wstring ReadText(std::wstring const& path) {
    std::wstring text;
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") || !file) return text;

    wchar_t buffer[4096];
    for (size_t read; (read = fread(buffer, sizeof(wchar_t), 4096, file)) > 0;) text.append(buffer, read);
    fclose(file);
    return text;
}

// Grava um texto como UTF-16 sem BOM.
static void WriteText(std::wstring const& path, std::wstring const& text) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") || !file) return;

    fwrite(text.data(), sizeof(wchar_t), text.size(), file);
    fclose(file);
}

// cache\contexto\lista.txt: chave da seleção na 1ª linha, GetTickCount64 de quando foi montada na 2ª e os itens depois.
// Devolve a chave (vazia sem arquivo); `written` e `items` ficam com o resto.
static std::wstring ReadList(std::wstring const& path, ULONGLONG& written, std::wstring& items) {
    std::wstring text = ReadText(path);
    size_t first = text.find(L'\n'), second = first == std::wstring::npos ? first : text.find(L'\n', first + 1);
    if (second == std::wstring::npos) return {};

    written = _wcstoui64(text.c_str() + first + 1, nullptr, 10);
    items = text.substr(second + 1);
    return text.substr(0, first);
}

// Última lista gravada, se tiver menos de `maxAge` ms, de qualquer seleção: devolve a chave dela (vazia se não houver).
std::wstring RecentLegacyItems(ULONGLONG maxAge, std::vector<std::shared_ptr<LegacyItem>>& items, ULONGLONG& written) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\TemaContextoLista");
    if (mutex) WaitForSingleObject(mutex, 10000);

    std::wstring text, key = ReadList(ProjectPath(L"cache\\contexto\\lista.txt"), written, text);
    if (!key.empty() && GetTickCount64() - written < maxAge) {
        items = ParseItems(text);
    } else {
        key.clear();
    }

    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return key;
}

// Lista de itens da seleção, dividida entre os processos: cada posição do menu roda num dllhost próprio (um por pacote),
// então o primeiro monta a lista e grava em cache\contexto\lista.txt, e os outros, se for a mesma seleção e de menos de
// 2 s atrás, só leem.
std::vector<std::shared_ptr<LegacyItem>> SharedLegacyItems(Selection const& selection, std::wstring const& key) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\TemaContextoLista");
    if (mutex) WaitForSingleObject(mutex, 10000);

    std::wstring folder = ProjectPath(L"cache\\contexto"), path = folder + L"\\lista.txt", text;
    ULONGLONG written = 0;
    bool fresh = ReadList(path, written, text) == key && GetTickCount64() - written < 2000;

    std::vector<std::shared_ptr<LegacyItem>> items;
    if (fresh) {
        items = ParseItems(text);
    } else {
        ULONGLONG started = GetTickCount64();
        Log(L"montando %s", key.c_str());
        items = CollectLegacyItems(selection);
        text = key + L"\n" + std::to_wstring(GetTickCount64()) + L"\n";
        WriteItems(text, items, 0);
        CreateDirectoryW(folder.c_str(), nullptr);
        WriteText(path, text);
        Log(L"%zu itens em %llu ms", items.size(), GetTickCount64() - started);
    }

    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return items;
}
