#include "taskbar/taskbar.h"
#include "common/log.h"
#include "common/registry.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

// Por thread do XAML, como o timer que relê.
// ponytail: guarda os pincéis de todas as barras já criadas (as velhas também, poucas por Explorer); weak_ref se crescer.
static thread_local std::vector<CompositionLinearGradientBrush> g_brushes;
static thread_local std::wstring g_colors;

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

// Cor `i` do degradê do logo (LogoCor1, LogoCor2 e LogoCor3, "#RRGGBB"; o app do Wallget grava): sem ela, branco,
// cinza e preto.
static winrt::Windows::UI::Color LogoColor(int i) {
    const wchar_t* names[3] = {L"LogoCor1", L"LogoCor2", L"LogoCor3"};
    const BYTE standard[3] = {255, 128, 0};
    std::wstring text = SettingText(names[i], L"");
    wchar_t* end = nullptr;
    unsigned long rgb = text.size() == 7 && text[0] == L'#' ? wcstoul(text.c_str() + 1, &end, 16) : 0;
    if (!end || *end) return {255, standard[i], standard[i], standard[i]};
    return {255, BYTE(rgb >> 16), BYTE(rgb >> 8), BYTE(rgb)};
}

// As três cores como texto, para saber quando o registro mudou.
static std::wstring LogoColors() {
    std::wstring text;
    for (int i = 0; i < 3; i++) {
        auto color = LogoColor(i);
        text += std::to_wstring(color.R << 16 | color.G << 8 | color.B) + L",";
    }
    return text;
}

// Põe no pincel as três cores: começo (topo à esquerda), meio e fim.
static void SetLogoStops(CompositionLinearGradientBrush const& brush) {
    auto c = brush.Compositor();
    brush.ColorStops().Clear();
    for (int i = 0; i < 3; i++) brush.ColorStops().Append(c.CreateColorGradientStop(i / 2.f, LogoColor(i)));
}

// Logo do Windows 11 com os cantos quase retos do original: um quadrado de 24 DIPs (o tamanho do ícone) cortado em
// quatro por uma cruz de 2 DIPs, com um degradê só, do topo à esquerda (branco, no padrão) até embaixo à direita
// (preto), passando pelo cinza.
static Visual MakeLogo(Compositor const& c) {
    auto brush = c.CreateLinearGradientBrush();
    brush.MappingMode(CompositionMappingMode::Absolute);
    brush.EndPoint({kSide, kSide});
    SetLogoStops(brush);
    g_brushes.push_back(brush);
    g_colors = LogoColors();

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

// O app do Wallget mudou as cores do logo no registro: troca o degradê na hora.
void RefreshStartLogo() {
    std::wstring colors = LogoColors();
    if (colors == g_colors) return;

    g_colors = colors;
    for (auto const& brush : g_brushes) SetLogoStops(brush);
}
