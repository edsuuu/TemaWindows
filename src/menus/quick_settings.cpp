#include "menus/menus.h"
#include "common/xaml_tree.h"

// Metade de cima das Configurações Rápidas: Border sem nome dentro do L1Grid.
static bool IsUpperHalfVeil(FrameworkElement const& element, std::wstring_view name, std::wstring_view type) {
    if (type != L"Windows.UI.Xaml.Controls.Border" || !name.empty()) return false;

    auto parent = ParentOf(element);
    return parent && parent.Name() == L"L1Grid";
}

// Parte da página principal (L1Grid): grupo dos controles deslizantes ou rodapé.
static bool IsMainPagePart(FrameworkElement const& element) {
    auto parent = ParentOf(element);
    return parent && parent.Name() == L"L1Grid";
}

// Elemento dentro do cabeçalho de uma subpágina (Wi-Fi, Bluetooth, Acessibilidade...), até `maxLevels` acima dele.
static bool IsPageHeaderPart(FrameworkElement const& element, int maxLevels) {
    int levels = 0;
    for (auto parent = ParentOf(element); parent && levels < maxLevels; parent = ParentOf(parent), levels++)
        if (parent.Name() == L"PageHeader") return true;
    return false;
}

// Na grade do modelo padrão do ToggleSwitch (colunas interruptor | vão de 12 | texto Ligado/Desligado, vazio no
// cabeçalho), zera a largura mínima de 154 e o vão.
static void CollapseToggleSpacer(DependencyObject const& element, int maxDepth) {
    for (int i = 0, count = VisualTreeHelper::GetChildrenCount(element); i < count; i++) {
        auto child = VisualTreeHelper::GetChild(element, i);
        auto grid = child.try_as<Controls::Grid>();
        if (grid && grid.ColumnDefinitions().Size() == 3) {
            if (grid.MinWidth() != 0) grid.MinWidth(0);
            if (grid.ColumnDefinitions().GetAt(1).Width().Value != 0) grid.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::FromPixels(0));
            return;
        }
        if (maxDepth > 1) CollapseToggleSpacer(child, maxDepth - 1);
    }
}

// Interruptor do cabeçalho à direita em todas as subpáginas, a 16 px da borda (o mesmo recuo dos ícones da lista).
// Algumas (Bluetooth) trocam, depois de carregar, para o modelo padrão do ToggleSwitch (largura mínima de 154 e vão
// para o texto), e o interruptor ficava no meio do cabeçalho. Chamado no Loaded e a cada troca de tamanho; não muda
// nada se já estiver certo.
static void AlignHeaderToggle(FrameworkElement const& toggle) {
    if (!IsPageHeaderPart(toggle, 10)) return;

    Thickness margin{8, 0, 8, 0};
    if (toggle.MinWidth() != 0) toggle.MinWidth(0);
    if (toggle.Margin() != margin) toggle.Margin(margin);
    CollapseToggleSpacer(toggle, 3);
}

// Voltar das subpáginas com o hover arredondado: canto de cima à esquerda 8, acompanhando a curva do painel; os outros
// 4. Chamado no Loaded: quando o botão nasce, o cabeçalho acima dele às vezes ainda não está ligado.
static void RoundBackButton(FrameworkElement const& button) {
    if (!IsPageHeaderPart(button, 4) || button.as<Controls::Control>().CornerRadius().TopLeft == 8) return;

    KeepValue(button, Controls::Control::CornerRadiusProperty(), winrt::box_value(CornerRadius{8, 4, 4, 4}), kMaxFights);
}

// Barra de tempo da música embaixo dos botões do cartão de mídia, numa linha nova da grade dele (uma vez só).
static void AddMediaTimeline(FrameworkElement const& element) {
    auto root = element.try_as<Controls::Grid>();
    if (!root) return;

    for (auto child : root.Children())
        if (auto existing = child.try_as<FrameworkElement>(); existing && existing.Name() == L"ThemeMediaTimeline") return;

    Controls::RowDefinition row;
    row.Height(GridLengthHelper::Auto());
    root.RowDefinitions().Append(row);

    auto timeline = CreateMediaTimeline();
    Controls::Grid::SetRow(timeline, root.RowDefinitions().Size() - 1);
    root.Children().Append(timeline);
}

// A engrenagem (rodapé) sobe para a linha do volume, à direita do botão do mixer, e a linha de baixo some: o rodapé
// sobe 57 DIPs (o centro dele encontra o da linha do volume) e o grupo dos controles deslizantes encolhe 44 à direita
// para abrir espaço. A engrenagem continua sendo o botão nativo (o clique é o do Windows).
static bool MoveGearToVolumeRow(FrameworkElement const& element, std::wstring_view name) {
    if ((name != L"SlidersGroup" && name != L"FooterGrid") || !IsMainPagePart(element)) return false;

    Thickness margin = name == L"SlidersGroup" ? Thickness{0, 0, 44, 0} : Thickness{0, -57, 8, 15};
    KeepValue(element, FrameworkElement::MarginProperty(), winrt::box_value(margin), kMaxFights);
    return true;
}

// Configurações Rápidas (ShellHost): o cartão de mídia e o dos controles viram vidro; o véu do cartão de mídia e o da
// metade de cima ficam transparentes; o cartão de mídia ganha a barra de tempo; a engrenagem vai para a linha do volume;
// e nas subpáginas o Voltar ganha o hover arredondado e o interruptor do cabeçalho fica colado à direita.
bool StyleQuickSettings(FrameworkElement const& element, std::wstring_view name, std::wstring_view type) {
    if (name == L"MediaTransportControlsRegion" || name == L"ControlCenterRegion") {
        ApplyPanelGlass(element);
        return true;
    }

    if (name == L"MediaTransportControlsRoot") {
        ClearVeil(element);
        AddMediaTimeline(element);
        return true;
    }

    if (IsUpperHalfVeil(element, name, type)) {
        ClearVeil(element);
        return true;
    }

    if (name == L"BackButton") {
        auto round = [](auto&& sender, auto&&) {
            try { RoundBackButton(sender.template as<FrameworkElement>()); } catch (...) {}
        };
        element.Loaded(round);
        if (element.IsLoaded()) round(element, nullptr);
        return true;
    }

    if (name == L"HeaderToggleSwitch") {
        auto align = [](auto&& sender, auto&&) {
            try { AlignHeaderToggle(sender.template as<FrameworkElement>()); } catch (...) {}
        };
        element.Loaded(align);
        element.SizeChanged(align);
        if (element.IsLoaded()) align(element, nullptr);
        return true;
    }

    return MoveGearToVolumeRow(element, name);
}
