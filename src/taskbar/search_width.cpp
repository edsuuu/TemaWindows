#include "taskbar/taskbar.h"
#include "common/log.h"
#include "common/xaml_tree.h"

#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Media.Animation.h>
#include <chrono>
#include <cmath>
#include <memory>

using namespace winrt::Windows::UI::Xaml::Media::Animation;

struct SearchFit {
    winrt::weak_ref<Controls::Primitives::ToggleButton> button;
    Controls::TextBlock text{nullptr};
    Storyboard story{nullptr};
    DoubleAnimation animation{nullptr};
    double full = 0, target = 0;
};

// Largura do texto sem corte (o TextBlock da caixa já vem limitado pela própria caixa): mede uma cópia com a mesma fonte.
static double TextWidth(Controls::TextBlock const& text) {
    Controls::TextBlock copy;
    copy.Text(text.Text());
    copy.FontFamily(text.FontFamily());
    copy.FontSize(text.FontSize());
    copy.FontWeight(text.FontWeight());
    copy.Measure({INFINITY, INFINITY});
    return std::ceil(copy.DesiredSize().Width);
}

// Fechada, a caixa fica do tamanho de "Pesquisar" (a margem do texto já traz a lupa à esquerda e o respiro à direita);
// com a pesquisa aberta (botão marcado) volta na hora à largura do Windows, com o layout refeito ali mesmo: o campo de
// texto do SearchHost mede a caixa no clique e, animando, saía cortado ("Pesquisar qu"). Ao fechar, anima a largura
// para os ícones da barra, que ficam centralizados, deslizarem em vez de pular.
static void Fit(FrameworkElement const& grid, SearchFit& fit) {
    if (!fit.text) {
        fit.text = FindDescendant(grid, L"SearchBoxTextBlock", 2).try_as<Controls::TextBlock>();
        auto button = FindAncestor(grid, L"SearchUx.SearchUI.SearchBoxButton", 3).try_as<Controls::Primitives::ToggleButton>();
        if (!fit.text || !button || !std::isfinite(grid.Width())) {
            fit.text = nullptr;
            return;
        }

        fit.full = grid.Width();
        fit.button = button;
        fit.animation = DoubleAnimation();
        fit.animation.Duration(DurationHelper::FromTimeSpan(std::chrono::milliseconds(180)));
        fit.animation.EasingFunction(CubicEase());
        fit.animation.EnableDependentAnimation(true);
        fit.story = Storyboard();
        fit.story.Children().Append(fit.animation);
        Storyboard::SetTarget(fit.animation, grid);
        Storyboard::SetTargetProperty(fit.animation, L"Width");
        if (auto background = FindDescendant(grid, L"BackgroundElement", 1)) KeepValue(background, FrameworkElement::MinWidthProperty(), winrt::box_value(0.0), 20);
    }

    auto button = fit.button.get();
    bool open = button && button.IsChecked() && button.IsChecked().Value();
    auto margin = fit.text.Margin();
    double target = open ? fit.full : std::min(fit.full, margin.Left + TextWidth(fit.text) + margin.Right);
    if (target == fit.target) return;

    fit.target = target;
    double from = grid.ActualWidth();
    fit.story.Stop();
    grid.Width(target);
    if (open) {
        grid.UpdateLayout();
    } else if (from > 0 && from != target) {
        fit.animation.From(from);
        fit.animation.To(target);
        fit.story.Begin();
    }
}

// Liga a caixa "Pesquisar" da barra: ajusta agora, a cada Loaded, ao abrir e fechar a pesquisa e se o texto mudar.
void ApplySearchWidth(FrameworkElement const& grid) {
    if (!grid) return;

    auto fit = std::make_shared<SearchFit>();
    auto run = [grid, fit](auto&&...) {
        try {
            bool hooked = bool(fit->text);
            Fit(grid, *fit);
            if (hooked || !fit->text) return;

            auto again = [grid, fit](auto&&...) {
                try { Fit(grid, *fit); } catch (...) { Log(L"erro na largura da pesquisa: %08X", winrt::to_hresult()); }
            };
            auto button = fit->button.get();
            button.Checked(again);
            button.Unchecked(again);
            fit->text.RegisterPropertyChangedCallback(Controls::TextBlock::TextProperty(), again);
            Log(L"caixa de pesquisa encolhida: %.0f de %.0f", fit->target, fit->full);
        } catch (...) {
            Log(L"erro na largura da pesquisa: %08X", winrt::to_hresult());
        }
    };
    run();
    grid.Loaded(run);
}
