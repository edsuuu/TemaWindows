#include "taskbar/taskbar.h"
#include "common/glass.h"
#include "common/log.h"

#include <algorithm>
#include <memory>

// Tamanho e cantos da cápsula seguem a caixa, mas os cantos só crescem: o estilo do modo caixa chega depois (nasce com
// cantos 4 e vira cápsula de 16) e, com a pesquisa aberta, o Windows volta para 4. Assim a cápsula é igual em todos
// os estados.
static void FitCapsule(Controls::Border const& box, CompositionRoundedRectangleGeometry const& shape, SpriteVisual const& glass,
                       float& maxRadius) {
    float2 size{(float)box.ActualWidth(), (float)box.ActualHeight()};
    if (size.x < 2 || size.y < 2) return;

    float radius = maxRadius = std::max(maxRadius, std::min((float)box.CornerRadius().TopLeft, size.y / 2));
    shape.CornerRadius({radius, radius});
    shape.Size(size);
    glass.Size(size);
    Log(L"caixa de pesquisa com vidro: %.0fx%.0f, cantos %.0f", size.x, size.y, radius);
}

// Zera a borda da caixa (a do Windows e a do estado ativo, com a pesquisa aberta): nenhum contorno.
static void RemoveBorder(DependencyObject const& o) {
    auto box = o.as<Controls::Border>();
    if (auto t = box.BorderThickness(); t.Left || t.Top || t.Right || t.Bottom) box.BorderThickness({});
}

// Cápsula de vidro cinza por cima do fundo da caixa "Pesquisar" (XAML do próprio Explorer, SearchUx), sem brilho e sem
// contorno (ele pediu nenhuma borda branca na pesquisa). O recorte vai no visual da própria caixa: corta também o fundo
// do Windows, que com a pesquisa aberta vira um retângulo de cantos 4 e escaparia pelos cantos. O anel de foco sai no
// entry, junto com o de todos os botões da barra.
void ApplySearchGlass(Controls::Border const& box) {
    if (ElementCompositionPreview::GetElementChildVisual(box)) return;

    auto c = ElementCompositionPreview::GetElementVisual(box).Compositor();
    auto shape = c.CreateRoundedRectangleGeometry();
    auto glass = c.CreateSpriteVisual();
    glass.Brush(TintedGlass(c));
    ElementCompositionPreview::GetElementVisual(box).Clip(c.CreateGeometricClip(shape));

    auto maxRadius = std::make_shared<float>(0.f);
    auto fit = [box, shape, glass, maxRadius](auto&&...) {
        try {
            FitCapsule(box, shape, glass, *maxRadius);
        } catch (...) {
            Log(L"erro na caixa de pesquisa: %08X", winrt::to_hresult());
        }
    };
    fit();
    box.SizeChanged(fit);
    box.RegisterPropertyChangedCallback(Controls::Border::CornerRadiusProperty(), fit);
    ElementCompositionPreview::SetElementChildVisual(box, glass);

    auto noBorder = [](DependencyObject const& o, auto&&...) {
        try { RemoveBorder(o); } catch (...) {}
    };
    noBorder(box);
    box.RegisterPropertyChangedCallback(Controls::Border::BorderThicknessProperty(), noBorder);
}
