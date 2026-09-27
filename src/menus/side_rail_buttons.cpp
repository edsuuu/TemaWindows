#include "menus/menus.h"
#include "common/log.h"

#include <shellapi.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Markup.h>

static const wchar_t* const kNavTemplateXaml = LR"(<ControlTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" TargetType="Button">
      <Grid>
        <VisualStateManager.VisualStateGroups>
          <VisualStateGroup x:Name="CommonStates">
            <VisualState x:Name="Normal"/>
            <VisualState x:Name="PointerOver"><VisualState.Setters>
              <Setter Target="BackgroundBorder.Background" Value="#0FFFFFFF"/></VisualState.Setters></VisualState>
            <VisualState x:Name="Pressed"><VisualState.Setters>
              <Setter Target="BackgroundBorder.Background" Value="#0AFFFFFF"/>
              <Setter Target="ContentPresenter.Foreground" Value="#C5FFFFFF"/></VisualState.Setters></VisualState>
            <VisualState x:Name="Disabled"><VisualState.Setters>
              <Setter Target="ContentPresenter.Foreground" Value="#5DFFFFFF"/></VisualState.Setters></VisualState>
          </VisualStateGroup>
        </VisualStateManager.VisualStateGroups>
        <Border x:Name="BackgroundBorder" Background="{TemplateBinding Background}" CornerRadius="4"/>
        <ContentPresenter x:Name="ContentPresenter" Content="{TemplateBinding Content}" Foreground="#FFFFFFFF"
            HorizontalAlignment="Center" VerticalAlignment="Center"/>
      </Grid>
    </ControlTemplate>)";

// Abre programa ou URI na hora, na thread da UI: numa thread à parte o Iniciar fecha, o processo congela e nada abre.
void Launch(std::wstring const& target) {
    ShellExecuteW(nullptr, nullptr, target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// O mesmo visual do botão nativo de energia/conta (StartDocked.NavigationPaneButton, lido dos estados dele): cantos 4,
// sem borda, fundo #0F branco no hover, #0A pressionado com texto a 77%, desabilitado a 36%. Montado uma vez só (o
// Iniciar tem uma thread de UI); se falhar, os botões ficam com o estilo padrão.
static Controls::ControlTemplate NavTemplate() {
    static Controls::ControlTemplate navTemplate{nullptr};
    static bool tried;
    if (navTemplate || tried) return navTemplate;

    tried = true;
    try {
        navTemplate = Markup::XamlReader::Load(kNavTemplateXaml).as<Controls::ControlTemplate>();
    } catch (...) {
        Log(L"template dos botões falhou: %08X", winrt::to_hresult());
    }
    return navTemplate;
}

// Ícone da fonte Segoe Fluent Icons.
static Controls::FontIcon FluentIcon(wchar_t glyph, double size) {
    Controls::FontIcon icon;
    icon.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
    icon.Glyph(std::wstring(1, glyph));
    icon.FontSize(size);
    return icon;
}

// Ícone com o rótulo embaixo (rótulo vazio = só o ícone).
static Controls::StackPanel ButtonContent(wchar_t glyph, const wchar_t* label) {
    Controls::StackPanel content;
    content.HorizontalAlignment(HorizontalAlignment::Center);
    content.Spacing(3);
    content.Children().Append(FluentIcon(glyph, *label ? 18 : 16));

    if (*label) {
        Controls::TextBlock text;
        text.Text(label);
        text.FontSize(11);
        text.HorizontalAlignment(HorizontalAlignment::Center);
        content.Children().Append(text);
    }
    return content;
}

// Botão da barra lateral: com rótulo é 52x52, sem rótulo 40x40 como o de energia nativo; o resto (cantos, cores,
// estados) é idêntico. "Selecionado" usa o mesmo fundo do hover nativo. A dica também vira o nome de acessibilidade.
Controls::Button SideButton(wchar_t glyph, const wchar_t* label, const wchar_t* tip, bool selected, std::function<void()> click) {
    Controls::Button button;
    button.Content(ButtonContent(glyph, label));
    if (auto navTemplate = NavTemplate()) button.Template(navTemplate);

    button.Width(*label ? 52 : 40);
    button.Height(*label ? 52 : 40);
    button.HorizontalAlignment(HorizontalAlignment::Center);
    button.Background(Solid(selected ? 0x0F : 0));
    Controls::ToolTipService::SetToolTip(button, winrt::box_value(winrt::hstring(tip)));
    Automation::AutomationProperties::SetName(button, tip);

    button.Click([click](auto&&, auto&&) {
        try { click(); } catch (...) {}
    });
    return button;
}
