#include "menus/menus.h"
#include "common/glass.h"
#include "common/registry.h"
#include "common/xaml_tree.h"

// Propriedades de fundo, contorno e cantos do elemento, conforme o tipo (Border, Grid ou Control).
BrushProperties BrushPropertiesOf(DependencyObject const& element) {
    using namespace Controls;

    if (element.try_as<Border>()) return {Border::BackgroundProperty(), Border::BorderBrushProperty(), Border::CornerRadiusProperty()};
    if (element.try_as<Grid>()) return {Panel::BackgroundProperty(), Grid::BorderBrushProperty(), Grid::CornerRadiusProperty()};
    if (element.try_as<Control>()) return {Control::BackgroundProperty(), Control::BorderBrushProperty(), Control::CornerRadiusProperty()};
    return {};
}

// Pincel sólido cinza (gray) com o alfa dado.
Media::SolidColorBrush Solid(uint8_t alpha, uint8_t gray) {
    return Media::SolidColorBrush(Color{alpha, gray, gray, gray});
}

// Cantos dos painéis de vidro (HKCU\Software\TemaBarra\VidroRaio, DIPs).
float GlassRadius() {
    return (float)Setting(L"VidroRaio", 20);
}

// Painel principal (Iniciar, cartões das Configurações Rápidas, notificações, calendário, pesquisa): vidro liso, sem
// contorno, cantos VidroRaio e o contorno especular com um brilho leve no topo, para o vidro aparecer mesmo escuro.
void ApplyPanelGlass(FrameworkElement const& element) {
    if (ElementCompositionPreview::GetElementChildVisual(element)) return;

    auto properties = BrushPropertiesOf(element);
    auto glass = GlassXamlBrush(ElementCompositionPreview::GetElementVisual(element).Compositor(), TintedGlass);
    float radius = GlassRadius();

    KeepValue(element, properties.background, glass, kMaxFights);
    KeepValue(element, properties.border, Solid(0), kMaxFights);
    KeepValue(element, properties.radius, winrt::box_value(CornerRadius{radius, radius, radius, radius}), kMaxFights);
    Sheen(element, radius, .8f, 16);
}

// Cartão de vidro dentro de um painel (categoria do Iniciar, notificação, caixa de pesquisa). radius 0 = mantém os
// cantos do próprio elemento (que precisa ser um Border).
void ApplyCard(FrameworkElement const& element, float radius, uint8_t fill, float rim) {
    if (ElementCompositionPreview::GetElementChildVisual(element)) return;

    auto properties = BrushPropertiesOf(element);
    KeepValue(element, properties.background, Solid(fill), kMaxFights);
    KeepValue(element, properties.border, Solid(0), kMaxFights);
    if (radius) KeepValue(element, properties.radius, winrt::box_value(CornerRadius{radius, radius, radius, radius}), kMaxFights);

    Sheen(element, radius ? radius : (float)element.as<Controls::Border>().CornerRadius().TopLeft, rim, 0);
}

// Véus claros que o shell põe por cima do acrílico somem e o vidro fica uma peça só. line = alfa do contorno que fica
// (0 = some; o do Iniciar vira um fio claro separando a lista da barra de baixo).
void ClearVeil(FrameworkElement const& element, uint8_t line) {
    auto properties = BrushPropertiesOf(element);

    KeepValue(element, properties.background, Solid(0), kMaxFights);
    KeepValue(element, properties.border, Solid(line), kMaxFights);
}

// Sombra e contorno de alto contraste acompanham os cantos do vidro.
void MatchGlassCorners(FrameworkElement const& border) {
    float radius = GlassRadius();

    KeepValue(border, Controls::Border::CornerRadiusProperty(), winrt::box_value(CornerRadius{radius, radius, radius, radius}), kMaxFights);
}
