#include "common/glass.h"
#include "common/log.h"
#include "common/registry.h"

#include <d2d1_1.h>
#include <d2d1effects.h>
#include <windows.graphics.effects.interop.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <algorithm>
#include <vector>

#pragma comment(lib, "dxguid.lib")

namespace abi = ABI::Windows::Graphics::Effects;
namespace wf = winrt::Windows::Foundation;

// Um efeito do Direct2D descrito para o Composition (o SDK não traz as classes prontas sem o Win2D). Regras que
// derrubam o processo se quebradas (conferidas pelo tools\effects-test): as fontes são sempre os mesmos objetos (o
// Composition percorre o grafo mais de uma vez; fonte nova a cada GetSource = 0xC0000005), as propriedades vão na
// ordem dos índices do D2D e os nomes são únicos no grafo.
struct D2DEffect : winrt::implements<D2DEffect, IGraphicsEffect, IGraphicsEffectSource, abi::IGraphicsEffectD2D1Interop> {
    winrt::hstring name;
    GUID id;
    std::vector<wf::IInspectable> properties;
    std::vector<IGraphicsEffectSource> sources;

    D2DEffect(winrt::hstring name, GUID id, std::vector<wf::IInspectable> properties, std::vector<IGraphicsEffectSource> sources)
        : name(name), id(id), properties(std::move(properties)), sources(std::move(sources)) {}

    winrt::hstring Name() { return name; }

    void Name(winrt::hstring const&) {}

    HRESULT STDMETHODCALLTYPE GetEffectId(GUID* out) override {
        *out = id;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(LPCWSTR, UINT*, abi::GRAPHICS_EFFECT_PROPERTY_MAPPING*) override {
        return E_INVALIDARG;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT* count) override {
        *count = (UINT)properties.size();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetProperty(UINT index, ABI::Windows::Foundation::IPropertyValue** value) override {
        if (index >= properties.size()) return E_BOUNDS;

        *value = static_cast<ABI::Windows::Foundation::IPropertyValue*>(winrt::detach_abi(properties[index].as<wf::IPropertyValue>()));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetSource(UINT index, abi::IGraphicsEffectSource** source) override {
        if (index >= sources.size()) return E_BOUNDS;

        winrt::copy_to_abi(sources[index], *reinterpret_cast<void**>(source));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetSourceCount(UINT* count) override {
        *count = (UINT)sources.size();
        return S_OK;
    }
};

// A fonte "o que está atrás", preenchida por EffectOnBackdrop.
IGraphicsEffectSource BackdropSource() {
    return CompositionEffectSourceParameter(L"backdrop");
}

// Desfoque gaussiano com borda "hard", para as beiradas não desbotarem.
IGraphicsEffect GaussianBlur(float sigma, IGraphicsEffectSource const& source) {
    std::vector<wf::IInspectable> properties{wf::PropertyValue::CreateSingle(sigma),
                                             wf::PropertyValue::CreateUInt32(D2D1_GAUSSIANBLUR_OPTIMIZATION_QUALITY),
                                             wf::PropertyValue::CreateUInt32(D2D1_BORDER_MODE_HARD)};

    return winrt::make<D2DEffect>(L"Blur", CLSID_D2D1GaussianBlur, properties, std::vector<IGraphicsEffectSource>{source});
}

// Vidro cinza liso: o fundo desfocado com uma camada RGB(14,14,16), cinza quase preto, de alfa `tint` (0-1) por cima.
IGraphicsEffect GlassEffect(float sigma, float tint) {
    float red = kGlassColor.R / 255.f, green = kGlassColor.G / 255.f, blue = kGlassColor.B / 255.f;
    std::vector<wf::IInspectable> color{wf::PropertyValue::CreateSingleArray({red, green, blue, tint})};
    auto layer = winrt::make<D2DEffect>(L"Tint", CLSID_D2D1Flood, color, std::vector<IGraphicsEffectSource>{});

    std::vector<wf::IInspectable> sourceOver{wf::PropertyValue::CreateUInt32(D2D1_COMPOSITE_MODE_SOURCE_OVER)};
    std::vector<IGraphicsEffectSource> sources{GaussianBlur(sigma, BackdropSource()).as<IGraphicsEffectSource>(), layer};
    return winrt::make<D2DEffect>(L"Mix", CLSID_D2D1Composite, sourceOver, sources);
}

// Pincel que aplica o efeito ao pincel de fundo dado (backdrop comum ou o host backdrop da janela).
CompositionBrush EffectOnBackdrop(Compositor const& compositor, IGraphicsEffect const& effect, CompositionBrush const& backdrop) {
    auto brush = compositor.CreateEffectFactory(effect).CreateBrush();
    brush.SetSourceParameter(L"backdrop", backdrop);
    return brush;
}

// Vidro da barra e dos menus. HKCU\Software\TemaBarra: VidroTinta (0-100, opacidade da camada, padrão 80) e VidroBlur
// (desvio do desfoque em DIPs, padrão 24; 0 = só a camada, sem desfoque).
CompositionBrush TintedGlass(Compositor const& compositor) {
    float tint = std::min<DWORD>(Setting(L"VidroTinta", 80), 100) / 100.f;
    DWORD sigma = Setting(L"VidroBlur", 24);

    if (sigma) return EffectOnBackdrop(compositor, GlassEffect((float)sigma, tint), compositor.CreateBackdropBrush());
    return compositor.CreateColorBrush({(uint8_t)(tint * 255), kGlassColor.R, kGlassColor.G, kGlassColor.B});
}

// Pincel XAML que cria o vidro do Composition quando o elemento aparece e o descarta quando some. Com a
// transparência do Windows desligada vira a cor sólida.
struct GlassBrush : Media::XamlCompositionBrushBaseT<GlassBrush> {
    Compositor compositor{nullptr};
    GlassBrushFactory factory;

    GlassBrush(Compositor const& compositor, GlassBrushFactory factory) : compositor(compositor), factory(factory) {}

    void OnConnected() {
        try {
            if (CompositionBrush()) return;

            if (!winrt::Windows::UI::ViewManagement::UISettings().AdvancedEffectsEnabled()) {
                CompositionBrush(compositor.CreateColorBrush(FallbackColor()));
                return;
            }

            CompositionBrush(factory(compositor));
        } catch (...) {
            Log(L"erro no pincel de vidro: %08X", winrt::to_hresult());
        }
    }

    void OnDisconnected() {
        try {
            if (auto brush = CompositionBrush()) {
                CompositionBrush(nullptr);
                brush.Close();
            }
        } catch (...) {}
    }
};

// Pincel XAML de vidro pronto para usar como Background, com a cor sólida de reserva.
Media::XamlCompositionBrushBase GlassXamlBrush(Compositor const& compositor, GlassBrushFactory factory) {
    auto brush = winrt::make<GlassBrush>(compositor, factory).as<Media::XamlCompositionBrushBase>();
    brush.FallbackColor(kGlassColor);
    return brush;
}

struct SheenLayer {
    CompositionRoundedRectangleGeometry geometry{nullptr};
    float inset;
};

// Degradê branco com os alfas (0-1) dados em cada parada.
static CompositionLinearGradientBrush WhiteGradient(Compositor const& c, float2 from, float2 to,
                                                    std::initializer_list<std::pair<float, float>> stops) {
    auto brush = c.CreateLinearGradientBrush();
    brush.StartPoint(from);
    brush.EndPoint(to);

    for (auto [offset, alpha] : stops)
        brush.ColorStops().Append(c.CreateColorGradientStop(offset, {(uint8_t)std::clamp(alpha * 255, 0.f, 255.f), 255, 255, 255}));
    return brush;
}

// Acrescenta ao visual um retângulo arredondado recuado `inset` da borda, com preenchimento e/ou contorno de 1 px.
static void AddSheenLayer(ShapeVisual const& visual, std::vector<SheenLayer>& layers, float radius, float inset,
                          CompositionBrush const& fill, CompositionBrush const& stroke) {
    auto c = visual.Compositor();
    auto geometry = c.CreateRoundedRectangleGeometry();
    float corner = std::max(0.f, radius - inset);
    geometry.CornerRadius({corner, corner});
    geometry.Offset({inset, inset});

    auto shape = c.CreateSpriteShape(geometry);
    if (fill) shape.FillBrush(fill);
    if (stroke) {
        shape.StrokeBrush(stroke);
        shape.StrokeThickness(1);
    }

    visual.Shapes().Append(shape);
    layers.push_back({geometry, inset});
}

// Acompanha o tamanho do elemento.
static void FitSheen(FrameworkElement const& element, ShapeVisual const& visual, std::vector<SheenLayer> const& layers) {
    float width = (float)element.ActualWidth(), height = (float)element.ActualHeight();

    visual.Size({width, height});
    for (auto& layer : layers)
        layer.geometry.Size({std::max(0.f, width - 2 * layer.inset), std::max(0.f, height - 2 * layer.inset)});
}

// Contorno especular suave (luz do alto à esquerda, reflexo mais fraco embaixo à direita) e brilho interno do topo,
// desenhados por cima do elemento. rim = força do contorno (0 = sem), glow = alfa do brilho do topo (0 = sem). O
// visual fica no elemento mesmo vazio: é a marca de "já aplicado".
void Sheen(FrameworkElement const& element, float radius, float rim, uint8_t glow) {
    auto host = ElementCompositionPreview::GetElementVisual(element);
    if (ElementCompositionPreview::GetElementChildVisual(element)) return;

    auto c = host.Compositor();
    auto visual = c.CreateShapeVisual();
    std::vector<SheenLayer> layers;

    if (glow)
        AddSheenLayer(visual, layers, radius, 0,
                      WhiteGradient(c, {0, 0}, {0, 1}, {{0.f, glow / 255.f}, {.1f, glow / 255.f * .45f}, {.35f, 0.f}}), nullptr);
    if (rim > 0)
        AddSheenLayer(visual, layers, radius, .5f, nullptr,
                      WhiteGradient(c, {0, 0}, {1, 1},
                                    {{0.f, .5f * rim}, {.25f, .18f * rim}, {.5f, .05f * rim}, {.8f, .07f * rim}, {1.f, .25f * rim}}));

    auto fit = [element, visual, layers](auto&&...) {
        try { FitSheen(element, visual, layers); } catch (...) {}
    };
    fit();
    element.SizeChanged(fit);
    ElementCompositionPreview::SetElementChildVisual(element, visual);
}
