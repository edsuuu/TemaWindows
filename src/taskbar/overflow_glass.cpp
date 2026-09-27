#include "taskbar/taskbar.h"
#include "common/glass.h"
#include "common/log.h"
#include "common/registry.h"
#include "common/xaml_tree.h"

#include <algorithm>

constexpr int kMaxFights = 30;
constexpr float kItemRadius = 4;

// Raio de um canto do hover de um ícone: se o canto do ícone está perto do canto do painel (vão menor que o raio do
// painel), acompanha a curva dele (raio do painel menos o vão); senão, o raio nativo.
static float CornerFor(float gapX, float gapY, float panelRadius) {
    float gap = std::max(gapX, gapY);
    return gap < panelRadius ? std::max(kItemRadius, panelRadius - gap) : kItemRadius;
}

// Põe os cantos na placa do hover do ícone (Grid#ContainerGrid > Border#BackgroundBorder, 40x40).
static void SetItemCorners(Controls::Control const& item, CornerRadius const& corners) {
    auto plate = FindDescendant(item, L"BackgroundBorder", 3).try_as<Controls::Border>();
    if (!plate || plate.CornerRadius() == corners) return;

    plate.CornerRadius(corners);
    if (corners != CornerRadius{kItemRadius, kItemRadius, kItemRadius, kItemRadius})
        Log(L"hover de ícone oculto na ponta: cantos %.0f/%.0f/%.0f/%.0f", corners.TopLeft, corners.TopRight, corners.BottomRight, corners.BottomLeft);
}

// Hover dos ícones das pontas acompanhando a curva do painel, como o Voltar das Configurações Rápidas: percorre a
// ilha do painel atrás dos ícones (SystemTray.NotifyIconView, dentro do WrapGrid) e calcula cada canto pela distância
// até a borda do painel.
static void RoundCornerItems(DependencyObject const& node, FrameworkElement const& panel, float radius) {
    for (int i = 0, count = VisualTreeHelper::GetChildrenCount(node); i < count; i++) {
        auto child = VisualTreeHelper::GetChild(node, i);
        auto item = child.try_as<Controls::Control>();
        if (!item || ClassOf(child) != L"SystemTray.NotifyIconView") {
            RoundCornerItems(child, panel, radius);
            continue;
        }

        auto p = item.TransformToVisual(panel).TransformPoint({0, 0});
        float right = float(panel.ActualWidth() - p.X - item.ActualWidth()), bottom = float(panel.ActualHeight() - p.Y - item.ActualHeight());
        SetItemCorners(item, {CornerFor(p.X, p.Y, radius), CornerFor(right, p.Y, radius), CornerFor(right, bottom, radius),
                              CornerFor(p.X, bottom, radius)});
    }
}

// O mesmo vidro dos menus no painel dos ícones ocultos (^), que é uma ilha XAML do próprio Explorer
// (Border#OverflowFlyoutBackgroundBorder, acrílico cinza de cantos 8): vidro cinza liso, sem a textura de ruído do
// acrílico, sem borda, cantos VidroRaio e um contorno especular suave. A cada layout, o hover dos ícones das pontas
// acompanha os cantos.
void ApplyOverflowGlass(Controls::Border const& border) {
    if (ElementCompositionPreview::GetElementChildVisual(border)) return;

    auto glass = GlassXamlBrush(ElementCompositionPreview::GetElementVisual(border).Compositor(), TintedGlass);
    float radius = (float)Setting(L"VidroRaio", 20);

    KeepValue(border, Controls::Border::BackgroundProperty(), glass, kMaxFights);
    KeepValue(border, Controls::Border::BorderBrushProperty(), Media::SolidColorBrush(Color{0, 0, 0, 0}), kMaxFights);
    KeepValue(border, Controls::Border::CornerRadiusProperty(), winrt::box_value(CornerRadius{radius, radius, radius, radius}), kMaxFights);
    Sheen(border, radius, .3f, 0);
    border.LayoutUpdated([border, radius](auto&&, auto&&) {
        try {
            if (auto root = border.XamlRoot()) RoundCornerItems(root.Content(), border, radius);
        } catch (...) {
            Log(L"erro nos cantos dos ícones ocultos: %08X", winrt::to_hresult());
        }
    });
    Log(L"ícones ocultos com vidro (%.0fx%.0f)", border.ActualWidth(), border.ActualHeight());
}
