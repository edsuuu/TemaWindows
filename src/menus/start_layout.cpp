#include "menus/menus.h"
#include "common/registry.h"
#include "common/xaml_tree.h"

#include <algorithm>
#include <cmath>

// Troca um texto do Iniciar e o mantém.
static void KeepText(FrameworkElement const& text, const wchar_t* value) {
    KeepValue(text, Controls::TextBlock::TextProperty(), winrt::box_value(value), kMaxFights);
}

// Cada seção (usados recentemente, recomendados) num cartão de vidro, com vão entre elas: blocos separados, como no
// conceito.
static void ApplySectionCard(FrameworkElement const& section) {
    auto properties = BrushPropertiesOf(section);

    KeepValue(section, FrameworkElement::MarginProperty(), winrt::box_value(Thickness{52, 4, 52, kPanelGap}), kMaxFights);
    if (properties.background) KeepValue(section, properties.background, Solid(0x0A), kMaxFights);
    KeepValue(section, FrameworkElement::MinHeightProperty(), winrt::box_value(0.0), kMaxFights);
    if (properties.radius) KeepValue(section, properties.radius, winrt::box_value(CornerRadius{12, 12, 12, 12}), kMaxFights);
}

// Partes que só existem com a barra lateral (MenusLateral): a própria barra, só a foto na conta (o nome some) e só o
// fio de cima no véu (a linha de baixo, do rodapé antigo, sai, e o véu desce até o fim).
static void ApplyRailParts(FrameworkElement const& element, std::wstring_view name) {
    if (name != L"MainMenu" && name != L"UserTileNameText" && name != L"AcrylicOverlay") return;
    if (!Enabled(L"MenusLateral")) return;

    if (name == L"MainMenu") {
        if (auto menu = element.try_as<Controls::Grid>()) BuildSideRailWhenReady(menu);
    } else if (name == L"UserTileNameText") {
        KeepValue(element, UIElement::VisibilityProperty(), winrt::box_value(Visibility::Collapsed), kMaxFights);
    } else {
        KeepValue(element, Controls::Border::BorderThicknessProperty(), winrt::box_value(Thickness{0, 1, 0, 0}), kMaxFights);
        KeepValue(element, Controls::Grid::RowSpanProperty(), winrt::box_value(2), kMaxFights);
    }
}

// Layout do conceito "Windows 12" no Iniciar: barra lateral, textos novos e seções em cartões. No SearchHost só o
// texto da caixa de pesquisa muda.
void ApplyStartLayout(FrameworkElement const& element, std::wstring_view name, bool startProcess) {
    if (!startProcess && name != L"PlaceholderTextContentPresenter") return;

    ApplyRailParts(element, name);

    if (name == L"PinnedListHeaderText") KeepText(element, L"Usados recentemente");
    else if (name == L"AllListHeadingText" || name == L"ZoomedOutHeading") KeepText(element, L"Pastas de apps");
    else if (name == L"PlaceholderText" || name == L"PlaceholderTextContentPresenter") KeepText(element, L"Pesquisar qualquer coisa");
    else if (name == L"StartMenuPinnedList" || name == L"TopLevelSuggestionsContainerParent") ApplySectionCard(element);
}

// Pastas de apps (visão Categoria) na largura toda: colunas iguais, cartões centrados em cada uma, alinhadas às margens
// do cartão "Recente". Só age quando muda: o novo ItemWidth refaz o layout uma vez e para.
void FillFolders(FrameworkElement const& appsGrid) {
    auto items = appsGrid.try_as<Controls::ItemsControl>();
    auto wrap = items ? items.ItemsPanelRoot().try_as<Controls::ItemsWrapGrid>() : nullptr;
    auto host = wrap ? VisualTreeHelper::GetParent(wrap).try_as<FrameworkElement>() : nullptr;
    if (!host || host.ActualWidth() <= 0) return;

    constexpr double side = 52;
    double available = host.ActualWidth() - 2 * side;
    int columns = std::max(1, (int)(available / 180));
    double width = std::floor(available / columns);
    if (!std::isnan(wrap.ItemWidth()) && std::abs(wrap.ItemWidth() - width) <= 1) return;

    wrap.ItemWidth(width);
    wrap.Margin({side, 0, side, 11});
}
