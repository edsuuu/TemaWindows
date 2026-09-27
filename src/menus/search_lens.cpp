#include "menus/menus.h"
#include "common/xaml_tree.h"

#include <cmath>

// Degradê do logo do Iniciar da barra: branco no topo à esquerda até um cinza médio embaixo à direita (aparece no
// vidro escuro).
static CompositionLinearGradientBrush LensGradient(Compositor const& c) {
    auto brush = c.CreateLinearGradientBrush();
    brush.MappingMode(CompositionMappingMode::Absolute);
    brush.StartPoint({1, 1});
    brush.EndPoint({15, 15});
    brush.ColorStops().Append(c.CreateColorGradientStop(0.f, Color{255, 255, 255, 255}));
    brush.ColorStops().Append(c.CreateColorGradientStop(1.f, Color{255, 112, 112, 118}));
    return brush;
}

// Lupa de 16x16 em traço: o aro e o cabo, com pontas redondas.
static ShapeVisual MakeLens(Compositor const& c) {
    auto brush = LensGradient(c);
    auto lens = c.CreateShapeVisual();
    lens.Size({16, 16});

    auto ring = c.CreateEllipseGeometry();
    ring.Center({6.5f, 6.5f});
    ring.Radius({4.8f, 4.8f});

    auto handle = c.CreateLineGeometry();
    handle.Start({10.2f, 10.2f});
    handle.End({14.6f, 14.6f});

    for (auto [geometry, width] : {std::pair<CompositionGeometry, float>{ring, 1.6f}, {handle, 2.f}}) {
        auto shape = c.CreateSpriteShape(geometry);
        shape.StrokeBrush(brush);
        shape.StrokeThickness(width);
        shape.StrokeStartCap(CompositionStrokeCap::Round);
        shape.StrokeEndCap(CompositionStrokeCap::Round);
        lens.Shapes().Append(shape);
    }
    return lens;
}

// Centraliza a lupa sobre o ícone original; só mexe no Composition quando a posição muda (não refaz layout).
static void CenterLens(FrameworkElement const& icon, UIElement const& host, Visual const& lens) {
    auto p = icon.TransformToVisual(host).TransformPoint({0, 0});
    float x = std::round(p.X + ((float)icon.ActualWidth() - 16) / 2);
    float y = std::round(p.Y + ((float)icon.ActualHeight() - 16) / 2);

    if (lens.Offset().x != x || lens.Offset().y != y) lens.Offset({x, y, 0});
}

// Desenha a lupa com degradê no lugar da lupa colorida nativa, do mesmo tamanho e posição, igual em todos os estados.
static void AddGradientLens(FrameworkElement const& icon) {
    auto host = VisualTreeHelper::GetParent(icon).try_as<UIElement>();
    if (!host || ElementCompositionPreview::GetElementChildVisual(host)) return;

    auto lens = MakeLens(ElementCompositionPreview::GetElementVisual(host).Compositor());
    ElementCompositionPreview::SetElementChildVisual(host, lens);

    auto center = [weakIcon = winrt::make_weak(icon), weakHost = winrt::make_weak(host), lens](auto&&...) {
        try {
            auto icon = weakIcon.get();
            auto host = weakHost.get();
            if (icon && host) CenterLens(icon, host, lens);
        } catch (...) {}
    };
    center();
    icon.LayoutUpdated(center);
}

// Tira a lupa colorida nativa (em todos os estados) e põe a nossa. O AnimatedIcon do SearchHost só fica transparente;
// as duas imagens do Iniciar (normal/foco) ficam sem imagem mas com o mesmo tamanho, para nada andar no layout. Uma
// lupa só por caixa: a imagem "Off" não ganha outra.
void ReplaceSearchLens(FrameworkElement const& element, std::wstring_view name) {
    if (name == L"SearchIconPlayer") {
        KeepValue(element, UIElement::OpacityProperty(), winrt::box_value(0.0), kMaxFights);
    } else {
        KeepValue(element, Controls::Image::SourceProperty(), nullptr, kMaxFights);
        KeepValue(element, FrameworkElement::WidthProperty(), winrt::box_value(16.0), kMaxFights);
        KeepValue(element, FrameworkElement::HeightProperty(), winrt::box_value(16.0), kMaxFights);
    }
    if (name == L"SearchIconOff") return;

    auto add = [](auto&& sender, auto&&) {
        try { AddGradientLens(sender.template as<FrameworkElement>()); } catch (...) {}
    };
    element.Loaded(add);
    if (element.IsLoaded()) add(element, nullptr);
}
