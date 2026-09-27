#include "menus/menus.h"
#include "common/xaml_tree.h"

#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <cmath>

// Popup aberta que contém o presenter.
static Controls::Primitives::Popup PopupOf(FrameworkElement const& presenter) {
    for (auto popup : VisualTreeHelper::GetOpenPopupsForXamlRoot(presenter.XamlRoot()))
        for (DependencyObject o = presenter; o; o = VisualTreeHelper::GetParent(o))
            if (o == popup.Child()) return popup;

    return nullptr;
}

// Leva o cartão/menu NATIVO (a Popup do flyout) para a direita da âncora, alinhado pelo topo (conta) ou pela base
// (energia). Conteúdo e comportamento continuam os do Windows; só a posição muda, e só se saiu do lugar.
static void PlaceBeside(FrameworkElement const& presenter, FrameworkElement const& anchor, bool alignBottom) {
    if (!anchor || !presenter.IsLoaded() || !anchor.IsLoaded() || !presenter.XamlRoot() || presenter.ActualWidth() == 0) return;

    auto popup = PopupOf(presenter);
    if (!popup) return;

    auto a = anchor.TransformToVisual(nullptr).TransformPoint({0, 0});
    auto p = presenter.TransformToVisual(nullptr).TransformPoint({0, 0});
    double dx = a.X + anchor.ActualWidth() + 8 - p.X;
    double dy = (alignBottom ? a.Y + anchor.ActualHeight() - presenter.ActualHeight() : a.Y) - p.Y;

    if (std::abs(dx) > 1) popup.HorizontalOffset(popup.HorizontalOffset() + dx);
    if (std::abs(dy) > 1) popup.VerticalOffset(popup.VerticalOffset() + dy);
}

// Menu de energia, reconhecido pelos textos dos itens (pt-BR ou inglês).
static bool IsPowerMenu(Controls::MenuFlyoutPresenter const& menu) {
    for (auto item : menu.Items())
        if (auto entry = item.try_as<Controls::MenuFlyoutItem>()) {
            winrt::hstring text = entry.Text();
            if (text == L"Reiniciar" || text == L"Desligar" || text == L"Restart" || text == L"Shut down") return true;
        }

    return false;
}

// Cartão da conta abre ao lado da foto na barra lateral; menu de energia, ao lado do botão de energia.
static void PlaceNativeFlyout(FrameworkElement const& presenter) {
    if (auto flyout = presenter.try_as<Controls::FlyoutPresenter>()) {
        auto content = flyout.Content().try_as<DependencyObject>();
        if (content && ClassOf(content).starts_with(L"AccountControl.")) PlaceBeside(presenter, g_avatarSlot.get(), false);
    } else if (auto menu = presenter.try_as<Controls::MenuFlyoutPresenter>()) {
        if (IsPowerMenu(menu)) PlaceBeside(presenter, g_powerSlot.get(), true);
    }
}

// Presenter de flyout que apareceu no Iniciar: a cada layout, se for o cartão da conta ou o menu de energia, abre ao
// lado da barra lateral. O nome marca o presenter para não empilhar handlers se ele voltar à árvore (o nativo não tem
// nome).
void WatchFlyout(FrameworkElement const& presenter) {
    if (presenter.Name() == L"ThemeFlyout") return;
    if (presenter.Name().empty()) presenter.Name(L"ThemeFlyout");

    presenter.LayoutUpdated([weak = winrt::make_weak(presenter)](auto&&, auto&&) {
        static bool busy;
        if (busy) return;

        busy = true;
        try {
            if (auto presenter = weak.get(); presenter && presenter.IsLoaded()) PlaceNativeFlyout(presenter);
        } catch (...) {}
        busy = false;
    });
}
