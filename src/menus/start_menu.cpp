#include "menus/menus.h"
#include "common/xaml_tree.h"

// Categoria da visão "Pastas de apps": Border sem nome dentro do RootGrid de um StartMenu.CategoryControl.
static bool IsCategoryCard(FrameworkElement const& element, std::wstring_view name, std::wstring_view type) {
    if (type != L"Windows.UI.Xaml.Controls.Border" || !name.empty()) return false;

    auto parent = ParentOf(element);
    return parent && parent.Name() == L"RootGrid" && ClassOf(ParentOf(parent)) == L"StartMenu.CategoryControl";
}

// Iniciar: o painel vira vidro, o véu de cima some deixando só um fio claro entre a lista e a barra de baixo, sombra e
// contorno acompanham os cantos e cada categoria das pastas vira um cartão.
bool StyleStartMenu(FrameworkElement const& element, std::wstring_view name, std::wstring_view type) {
    if (name == L"AcrylicBorder") {
        ApplyPanelGlass(element);
        return true;
    }

    if (name == L"AcrylicOverlay") {
        ClearVeil(element, 0x12);
        return true;
    }

    if (name == L"StartDropShadow" || name == L"MainMenuHighContrastBorder") {
        MatchGlassCorners(element);
        return true;
    }

    if (IsCategoryCard(element, name, type)) {
        ApplyCard(element, 16, 0x0C);
        return true;
    }

    return false;
}
