#include "settings/settings_glass.h"
#include "common/glass.h"
#include "common/log.h"
#include "common/registry.h"
#include "common/xaml_tree.h"

#include <algorithm>

// Vidro das Configurações: o que está atrás da janela (host backdrop) desfocado, com a camada quase preta JanelasTinta
// (0-100, padrão 80) por cima; desfoque VidroBlur, o mesmo dos menus. Não depende da janela estar em foco.
static CompositionBrush SettingsGlass(Compositor const& compositor) {
    float tint = std::min<DWORD>(Setting(L"JanelasTinta", 80), 100) / 100.f;
    float sigma = (float)Setting(L"VidroBlur", 24);

    return EffectOnBackdrop(compositor, GlassEffect(sigma, tint), compositor.CreateHostBackdropBrush());
}

// O vidro vai no fundo da página raiz, que cobre a janela inteira: o XAML dela já é transparente e o fundo escuro vem
// do ApplicationFrameHost.
void ApplySettingsGlass(DependencyObject const& rootPage) {
    auto glass = GlassXamlBrush(Window::Current().Compositor(), SettingsGlass);

    KeepValue(rootPage, Controls::Control::BackgroundProperty(), glass, 30);
    Log(L"vidro nas Configurações");
}
