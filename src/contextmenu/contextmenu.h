#pragma once

#include <windows.h>
#include <shobjidl_core.h>
#include <memory>
#include <string>
#include <vector>

struct Selection {
    bool background = false;
    std::vector<std::wstring> paths;
};

struct LegacyItem {
    std::wstring title;
    std::wstring icon;
    std::wstring verb;
    CLSID handler{};
    std::wstring handlerKey;
    std::vector<int> position;
    std::wstring program;
    std::wstring arguments;
    std::vector<std::shared_ptr<LegacyItem>> children;
};

bool ReadSelection(IShellItemArray* items, bool background, Selection& selection);
std::vector<std::shared_ptr<LegacyItem>> CollectLegacyItems(Selection const& selection);
std::vector<std::shared_ptr<LegacyItem>> SharedLegacyItems(Selection const& selection, std::wstring const& key);
void InvokeLegacyItem(Selection const& selection, LegacyItem const& item);
std::wstring SaveMenuIcon(HBITMAP bitmap);
