#include "taskbar/taskbar.h"
#include "common/glass.h"
#include "common/log.h"
#include "common/registry.h"
#include "common/xaml_tree.h"

constexpr int kMaxFights = 30;

// O mesmo vidro dos menus no painel dos ícones ocultos (^), que é uma ilha XAML do próprio Explorer
// (Border#OverflowFlyoutBackgroundBorder, acrílico cinza de cantos 8): vidro cinza liso, sem a textura de ruído do
// acrílico, sem borda, cantos VidroRaio e um contorno especular suave.
void ApplyOverflowGlass(Controls::Border const& border) {
    if (ElementCompositionPreview::GetElementChildVisual(border)) return;

    auto glass = GlassXamlBrush(ElementCompositionPreview::GetElementVisual(border).Compositor(), TintedGlass);
    float radius = (float)Setting(L"VidroRaio", 20);

    KeepValue(border, Controls::Border::BackgroundProperty(), glass, kMaxFights);
    KeepValue(border, Controls::Border::BorderBrushProperty(), Media::SolidColorBrush(Color{0, 0, 0, 0}), kMaxFights);
    KeepValue(border, Controls::Border::CornerRadiusProperty(), winrt::box_value(CornerRadius{radius, radius, radius, radius}), kMaxFights);
    Sheen(border, radius, .3f, 0);
    Log(L"ícones ocultos com vidro (%.0fx%.0f)", border.ActualWidth(), border.ActualHeight());
}
