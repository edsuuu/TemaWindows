#include "menus/menus.h"
#include "common/xaml_tree.h"

// Janela "Pesquisar" do SearchHost (cobre o Iniciar quando se digita ou se clica em Pesquisar na barra): o painel vira
// vidro, a camada de cima some e a sombra acompanha os cantos.
static bool StyleSearchPanel(FrameworkElement const& element, std::wstring_view name) {
    bool glass = name == L"AppBorder" || name == L"AccentAppBorder";
    bool veil = name == L"LayerBorder" || name == L"AccentLayerBorder";
    bool shadow = name == L"dropshadow" || name == L"HCBorder";
    if (!glass && !veil && !shadow) return false;

    auto parent = ParentOf(element);
    if (!parent || (parent.Name() != L"BorderGrid" && parent.Name() != L"HCOuterBorderGrid")) return false;

    if (glass) ApplyPanelGlass(element);
    else if (veil) ClearVeil(element);
    else MatchGlassCorners(element);
    return true;
}

// Caixa de pesquisa do Iniciar ou do SearchHost, quando o template já está completo (depois do Loaded): sem contorno
// (o estado de foco anima o BorderBrush, então zera a espessura) e com cápsula de vidro. No SearchHost a cápsula é o
// TaskbarSearchBackground, então o fundo desta fica transparente.
static void StyleSearchBox(FrameworkElement const& border) {
    auto owner = ClassOf(ParentOf(ParentOf(border)));
    bool start = owner == L"StartMenu.SearchBoxToggleButton";
    if (!start && owner != L"Cortana.UI.Views.CortanaRichSearchBox") return;
    if (ElementCompositionPreview::GetElementChildVisual(border)) return;

    KeepValue(border, Controls::Border::BorderThicknessProperty(), winrt::box_value(Thickness{}), kMaxFights);
    ApplyCard(border, 0, start ? 0x10 : 0, 0);
}

// Pesquisa: a lupa com degradê, o painel da janela "Pesquisar" e as caixas de pesquisa (cápsulas de vidro sem contorno).
bool StyleSearch(FrameworkElement const& element, std::wstring_view name) {
    if (name == L"SearchIconOn" || name == L"SearchIconOff" || name == L"SearchIconPlayer") {
        ReplaceSearchLens(element, name);
        return true;
    }

    if (name == L"TaskbarSearchBackground") {
        KeepValue(element, Controls::Border::BorderThicknessProperty(), winrt::box_value(Thickness{}), kMaxFights);
        ApplyCard(element, 0, 0x10, 0);
        return true;
    }

    if (name == L"BorderElement") {
        element.Loaded([](auto&& sender, auto&&) {
            try { StyleSearchBox(sender.as<FrameworkElement>()); } catch (...) {}
        });
        return true;
    }

    return StyleSearchPanel(element, name);
}
