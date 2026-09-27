#include "taskbar/taskbar.h"

#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <string>

// Esconde o indicador original (sempre: botão reciclado pelo ItemsRepeater volta com ele à mostra) e copia para o traço
// se o original está pintado e com que opacidade.
static void SyncIndicator(FrameworkElement const& original, ShapeVisual const& stroke) {
    ElementCompositionPreview::GetElementVisual(original).TransformMatrix(winrt::Windows::Foundation::Numerics::make_float4x4_scale(0.f));

    auto fill = original.as<Shapes::Shape>().Fill();
    auto solid = fill.try_as<Media::SolidColorBrush>();
    bool painted = original.Visibility() == Visibility::Visible && fill && fill.Opacity() > 0 && (!solid || solid.Color().A > 0);
    stroke.Properties().InsertScalar(L"v", painted ? (float)original.Opacity() : 0.f);
}

// Liga uma propriedade do traço (ou da geometria dele) a uma expressão sobre o visual do indicador original (src) e as
// propriedades do traço (p).
static void Mirror(CompositionObject const& target, const wchar_t* property, const wchar_t* expression, Visual const& source,
                   ShapeVisual const& stroke) {
    auto animation = source.Compositor().CreateExpressionAnimation(expression);
    animation.SetReferenceParameter(L"src", source);
    animation.SetReferenceParameter(L"p", stroke.Properties());
    target.StartAnimation(property, animation);
}

// Traço no branco translúcido dos apps abertos (cinza sobre a barra) que espelha, no compositor, posição, tamanho,
// escala e opacidade do visual do indicador original: é por aí que o Windows mostra, alarga e some com o indicador (ao
// fechar um app os valores do XAML nem mudam).
static ShapeVisual CreateMirrorStroke(FrameworkElement const& original, winrt::hstring const& owner) {
    auto source = ElementCompositionPreview::GetElementVisual(original);
    auto c = source.Compositor();
    auto geometry = c.CreateRoundedRectangleGeometry();
    auto shape = c.CreateSpriteShape(geometry);
    shape.FillBrush(c.CreateColorBrush({0x8B, 255, 255, 255}));

    auto stroke = c.CreateShapeVisual();
    stroke.Comment(owner);
    stroke.Shapes().Append(shape);
    stroke.Properties().InsertScalar(L"v", 0);

    Mirror(stroke, L"Offset", L"src.Offset", source, stroke);
    Mirror(stroke, L"Size", L"src.Size", source, stroke);
    Mirror(stroke, L"CenterPoint", L"src.CenterPoint", source, stroke);
    Mirror(stroke, L"Scale", L"src.Scale", source, stroke);
    Mirror(stroke, L"Opacity", L"Min(src.Opacity, p.v)", source, stroke);
    Mirror(geometry, L"Size", L"src.Size", source, stroke);
    Mirror(geometry, L"CornerRadius", L"Vector2(src.Size.Y / 2, src.Size.Y / 2)", source, stroke);
    return stroke;
}

// Indicador de execução no cinza dos outros também no app ativo: o Windows pinta o do ativo com a cor de destaque (rosa
// quando o destaque automático pega o rosa do papel de parede) e anima a cor por cima de qualquer valor nosso. Um traço
// nosso, irmão do original no mesmo painel, faz o papel dele; só a cor é nossa. O traço guarda de qual indicador é
// cópia: botão reciclado com o template refeito traz um indicador novo, e um traço ainda ligado ao antigo deixava o
// app aberto sem barrinha.
void MirrorRunningIndicator(FrameworkElement const& original) {
    auto panel = VisualTreeHelper::GetParent(original).as<UIElement>();
    auto stroke = ElementCompositionPreview::GetElementChildVisual(panel).try_as<ShapeVisual>();
    winrt::hstring owner = std::to_wstring(reinterpret_cast<uintptr_t>(winrt::get_abi(original))).c_str();

    if (!stroke || stroke.Comment() != owner) {
        stroke = CreateMirrorStroke(original, owner);
        ElementCompositionPreview::SetElementChildVisual(panel, stroke);

        auto sync = [original, stroke](auto&&...) {
            try { SyncIndicator(original, stroke); } catch (...) {}
        };
        for (auto property : {UIElement::OpacityProperty(), UIElement::VisibilityProperty(), Shapes::Shape::FillProperty()})
            original.RegisterPropertyChangedCallback(property, sync);
    }

    SyncIndicator(original, stroke);
}
