#include "taskbar/taskbar.h"
#include "common/glass.h"
#include "common/log.h"
#include "common/registry.h"
#include "common/xaml_tree.h"

#include <dwmapi.h>
#include <algorithm>
#include <string>
#include <vector>

// Por thread do XAML, como o timer que relê (cada thread só pode mexer nos elementos dela).
static thread_local std::vector<winrt::weak_ref<FrameworkElement>> g_backgrounds;
static thread_local std::wstring g_look;

// Esconde o preenchimento e a linha do topo da barra; se o Explorer trouxer de volta, somem de novo.
static void HideNativeBackground(FrameworkElement const& background) {
    for (auto name : {L"BackgroundFill", L"BackgroundStroke"}) {
        auto element = FindDescendant(background, name, 3);
        if (!element || element.Opacity() == 0) continue;

        element.Opacity(0);
        element.RegisterPropertyChangedCallback(UIElement::OpacityProperty(), [](DependencyObject const& o, auto&&) {
            o.as<UIElement>().Opacity(0);
        });
    }
}

// Janela Win32 só ganha o host backdrop se pedir ao DWM: pede nas barras (principal e secundárias) deste Explorer.
static void EnableHostBackdrop() {
    EnumWindows([](HWND window, LPARAM) -> BOOL {
        wchar_t className[64];
        DWORD pid;
        GetWindowThreadProcessId(window, &pid);
        if (pid != GetCurrentProcessId() || !GetClassNameW(window, className, 64)) return TRUE;

        if (!wcscmp(className, L"Shell_TrayWnd") || !wcscmp(className, L"Shell_SecondaryTrayWnd")) {
            BOOL on = TRUE;
            DwmSetWindowAttribute(window, DWMWA_USE_HOSTBACKDROPBRUSH, &on, sizeof on);
        }
        return TRUE;
    }, 0);
}

// BlurModo 0 = o "host backdrop" forte do acrílico; 1 = gaussiano leve, igual ao modo "blur" do TranslucentTB, de raio
// BlurRaio px (2 = transparente nem chega aqui). A janela da barra é transparente, então o backdrop comum já é o que
// está atrás dela.
static CompositionBrush BlurBrush(Compositor const& c, DWORD mode, DWORD radius) {
    if (mode != 0) return EffectOnBackdrop(c, GaussianBlur(radius / 3.f, BackdropSource()), c.CreateBackdropBrush());

    auto brush = c.CreateHostBackdropBrush();
    EnableHostBackdrop();
    return brush;
}

// Visual que cobre o elemento inteiro com o pincel dado.
static SpriteVisual FullSizeSprite(Compositor const& c, CompositionBrush const& brush) {
    auto sprite = c.CreateSpriteVisual();
    sprite.RelativeSizeAdjustment({1, 1});
    sprite.Brush(brush);
    return sprite;
}

// Modo, raio e véu do fundo como texto, para saber quando o registro mudou.
static std::wstring Look() {
    return std::to_wstring(Setting(L"BlurModo", 1)) + L"," + std::to_wstring(Setting(L"BlurRaio", 9)) + L"," + std::to_wstring(Setting(L"BlurVeu", 31));
}

// Camadas do fundo: o desfoque (nenhum com BlurModo 2, transparente) e um véu preto de alfa BlurVeu (0-255) por cima,
// para os ícones continuarem legíveis.
static void BuildLayers(FrameworkElement const& background) {
    DWORD mode = Setting(L"BlurModo", 1), radius = Setting(L"BlurRaio", 9);
    BYTE veil = (BYTE)std::min<DWORD>(Setting(L"BlurVeu", 31), 255);
    auto c = ElementCompositionPreview::GetElementVisual(background).Compositor();

    auto layers = c.CreateContainerVisual();
    layers.RelativeSizeAdjustment({1, 1});
    if (mode != 2) layers.Children().InsertAtTop(FullSizeSprite(c, BlurBrush(c, mode, radius)));
    layers.Children().InsertAtTop(FullSizeSprite(c, c.CreateColorBrush({veil, 0, 0, 0})));

    ElementCompositionPreview::SetElementChildVisual(background, layers);
    Log(L"fundo da barra aplicado (modo %lu, raio %lu, véu %u)", mode, radius, veil);
}

// Troca o fundo da barra por um desfoque do que está atrás (no lugar do TranslucentTB), ou só o véu (transparente).
void ApplyBlurBackground(FrameworkElement const& background) {
    HideNativeBackground(background);
    if (ElementCompositionPreview::GetElementChildVisual(background)) return;

    BuildLayers(background);
    g_backgrounds.push_back(winrt::make_weak(background));
    g_look = Look();
}

// O app do Wallget mudou o fundo no registro: refaz as camadas das barras que ainda existem, sem reiniciar o Explorer.
void RefreshBlurBackground() {
    std::wstring look = Look();
    if (look == g_look) return;

    g_look = look;
    std::erase_if(g_backgrounds, [](winrt::weak_ref<FrameworkElement> const& weak) {
        auto background = weak.get();
        if (background) BuildLayers(background);
        return !background;
    });
}
