#include "taskbar/taskbar.h"
#include "common/log.h"

#include <cmath>

constexpr float kSquare = 11;
constexpr float kGap = 2;
constexpr float kSide = kSquare * 2 + kGap;
constexpr float kCornerRadius = 1;

// Um quarto do logo: o quadrado inteiro arredondado, recortado só no pedaço que começa em (x, y). Assim o degradê
// atravessa os quatro pedaços como se fosse uma peça só.
static ShapeVisual MakeLogoQuarter(Compositor const& c, CompositionBrush const& brush, float x, float y) {
    auto geometry = c.CreateRoundedRectangleGeometry();
    geometry.Size({kSide, kSide});
    geometry.CornerRadius({kCornerRadius, kCornerRadius});

    auto shape = c.CreateSpriteShape(geometry);
    shape.FillBrush(brush);

    auto quarter = c.CreateShapeVisual();
    quarter.Size({kSide, kSide});
    quarter.Shapes().Append(shape);
    quarter.Clip(c.CreateInsetClip(x, y, kSide - x - kSquare, kSide - y - kSquare));
    return quarter;
}

// Logo do Windows 11 com os cantos quase retos do original: um quadrado de 24 DIPs (o tamanho do ícone) cortado em
// quatro por uma cruz de 2 DIPs, com um degradê só, do branco (topo à esquerda) ao preto.
static Visual MakeLogo(Compositor const& c) {
    auto brush = c.CreateLinearGradientBrush();
    brush.MappingMode(CompositionMappingMode::Absolute);
    brush.EndPoint({kSide, kSide});
    brush.ColorStops().Append(c.CreateColorGradientStop(0.f, {255, 255, 255, 255}));
    brush.ColorStops().Append(c.CreateColorGradientStop(1.f, {255, 0, 0, 0}));

    auto logo = c.CreateContainerVisual();
    logo.Size({kSide, kSide});
    for (float y : {0.f, kSquare + kGap})
        for (float x : {0.f, kSquare + kGap}) logo.Children().InsertAtTop(MakeLogoQuarter(c, brush, x, y));
    return logo;
}

// Centraliza o logo sobre o ícone original, em pixels inteiros.
static void CenterLogo(FrameworkElement const& icon, UIElement const& parent, Visual const& logo) {
    auto p = icon.TransformToVisual(parent).TransformPoint({0, 0});
    float side = logo.Size().x;

    logo.Offset({std::floor(p.X + ((float)icon.ActualWidth() - side) / 2), std::floor(p.Y + ((float)icon.ActualHeight() - side) / 2), 0});
}

// Põe o logo por cima do ícone do Iniciar (que fica transparente) e o mantém centralizado quando o layout muda. Sem
// ampliação no logo: ele pediu.
void ApplyStartLogo(FrameworkElement const& icon) {
    auto parent = VisualTreeHelper::GetParent(icon).try_as<UIElement>();
    if (!parent || ElementCompositionPreview::GetElementChildVisual(parent)) return;

    auto logo = MakeLogo(ElementCompositionPreview::GetElementVisual(parent).Compositor());
    ElementCompositionPreview::SetElementChildVisual(parent, logo);
    icon.Opacity(0);

    auto center = [icon, parent, logo](auto&&...) {
        try { CenterLogo(icon, parent, logo); } catch (...) {}
    };
    center();
    icon.SizeChanged(center);
    parent.as<FrameworkElement>().SizeChanged(center);
    Log(L"logo do Iniciar aplicado");
}
