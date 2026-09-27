#include "menus/menus.h"
#include "common/log.h"
#include "common/registry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>

// Pincel em texto: classe e, para cor sólida ou acrílico, as cores e opacidades.
static std::wstring BrushInfo(Media::Brush const& brush) {
    if (!brush) return L"";

    std::wstring info = L" bg=" + std::wstring(winrt::get_class_name(brush));
    wchar_t buffer[128];

    if (auto solid = brush.try_as<Media::SolidColorBrush>()) {
        auto c = solid.Color();
        swprintf_s(buffer, L"(%02X%02X%02X%02X o%.2f)", c.A, c.R, c.G, c.B, solid.Opacity());
        info += buffer;
    } else if (auto acrylic = brush.try_as<Media::AcrylicBrush>()) {
        auto c = acrylic.TintColor();
        auto luminosity = acrylic.TintLuminosityOpacity();
        auto fallback = acrylic.FallbackColor();
        swprintf_s(buffer, L"(tint %02X%02X%02X%02X to%.2f lum%.2f src%d fb%02X%02X%02X%02X)", c.A, c.R, c.G, c.B, acrylic.TintOpacity(),
                   luminosity ? luminosity.Value() : -1.0, (int)acrylic.BackgroundSource(), fallback.A, fallback.R, fallback.G, fallback.B);
        info += buffer;
    }
    return info;
}

// Layout do elemento em texto: colapsado, opacidade, margem, linha e coluna da grade e alinhamento horizontal.
static std::wstring LayoutInfo(FrameworkElement const& element) {
    std::wstring info;
    wchar_t buffer[128];

    if (element.Visibility() == Visibility::Collapsed) info += L" [colapsado]";
    if (element.Opacity() < 1) {
        swprintf_s(buffer, L" op%.2f", element.Opacity());
        info += buffer;
    }

    auto m = element.Margin();
    if (m.Left || m.Top || m.Right || m.Bottom) {
        swprintf_s(buffer, L" m(%.0f,%.0f,%.0f,%.0f)", m.Left, m.Top, m.Right, m.Bottom);
        info += buffer;
    }

    int row = Controls::Grid::GetRow(element), span = Controls::Grid::GetRowSpan(element);
    if (row || span > 1) {
        swprintf_s(buffer, L" row%d/%d", row, span);
        info += buffer;
    }

    int column = Controls::Grid::GetColumn(element), columnSpan = Controls::Grid::GetColumnSpan(element);
    if (column || columnSpan > 1) {
        swprintf_s(buffer, L" col%d/%d", column, columnSpan);
        info += buffer;
    }

    if (!std::isnan(element.Height()) || element.MinHeight() > 0 || element.MaxHeight() < 1e6) {
        swprintf_s(buffer, L" h=%.0f/%.0f/%.0f", element.Height(), element.MinHeight(), std::min(element.MaxHeight(), 1e6));
        info += buffer;
    }

    if (!std::isnan(element.Width()) || element.MinWidth() > 0) {
        swprintf_s(buffer, L" w=%.0f/%.0f", element.Width(), element.MinWidth());
        info += buffer;
    }

    const wchar_t* alignments[4] = {L"L", L"C", L"R", L""};
    if (element.HorizontalAlignment() != HorizontalAlignment::Stretch) info += std::wstring(L" ha=") + alignments[(int)element.HorizontalAlignment()];
    return info;
}

// Colunas da grade em texto: Auto, * (proporcional) ou largura fixa.
static std::wstring ColumnsInfo(Controls::Grid const& grid) {
    std::wstring info;
    wchar_t buffer[32];

    for (auto column : grid.ColumnDefinitions()) {
        auto width = column.Width();
        if (width.GridUnitType == GridUnitType::Auto) swprintf_s(buffer, L"A,");
        else if (width.GridUnitType == GridUnitType::Star) swprintf_s(buffer, L"%.0f*,", width.Value);
        else swprintf_s(buffer, L"%.0f,", width.Value);
        info += buffer;
    }
    return info.empty() ? L"" : L" cols=[" + info.substr(0, info.size() - 1) + L"]";
}

// Linhas da grade, texto, cantos e pincéis do elemento em texto.
static std::wstring ContentInfo(DependencyObject const& o) {
    std::wstring info;
    wchar_t buffer[64];

    if (auto grid = o.try_as<Controls::Grid>()) {
        if (grid.RowDefinitions().Size()) {
            swprintf_s(buffer, L" rows=%u", grid.RowDefinitions().Size());
            info += buffer;
        }
        info += ColumnsInfo(grid);
    }
    if (auto text = o.try_as<Controls::TextBlock>()) info += L" \"" + std::wstring(text.Text()).substr(0, 40) + L"\"";

    if (auto border = o.try_as<Controls::Border>()) {
        swprintf_s(buffer, L" r%.0f", border.CornerRadius().TopLeft);
        info += buffer + BrushInfo(border.Background());
        if (border.BorderBrush()) info += L" stroke" + BrushInfo(border.BorderBrush()).substr(3);
    } else if (auto presenter = o.try_as<Controls::ContentPresenter>(); presenter && presenter.CornerRadius().TopLeft) {
        swprintf_s(buffer, L" r%.0f", presenter.CornerRadius().TopLeft);
        info += buffer + BrushInfo(presenter.Background());
    } else if (auto panel = o.try_as<Controls::Panel>()) {
        info += BrushInfo(panel.Background());
    } else if (auto control = o.try_as<Controls::Control>()) {
        info += BrushInfo(control.Background());
    }
    return info;
}

// Uma linha por elemento, recuada pela profundidade, e depois os filhos (até 200 níveis, só por segurança).
static void Dump(DependencyObject const& o, int depth, std::wstring& out) {
    auto element = o.try_as<FrameworkElement>();
    wchar_t buffer[512];

    swprintf_s(buffer, L"%*s%s#%s %.0fx%.0f", depth * 2, L"", winrt::get_class_name(o).c_str(), element ? element.Name().c_str() : L"",
               element ? element.ActualWidth() : 0, element ? element.ActualHeight() : 0);
    out += buffer;
    if (element) out += LayoutInfo(element);
    out += ContentInfo(o) + L"\n";

    if (depth < 200)
        for (int i = 0, count = VisualTreeHelper::GetChildrenCount(o); i < count; i++) Dump(VisualTreeHelper::GetChild(o, i), depth + 1, out);
}

// Despeja no log a árvore inteira da raiz (Window da CoreWindow ou conteúdo de uma ilha XAML) e as popups abertas.
static void DumpTree(winrt::Windows::Foundation::IInspectable const& root) {
    auto window = root.try_as<Window>();
    auto island = root.try_as<Hosting::DesktopWindowXamlSource>();
    auto content = window ? window.Content() : island ? island.Content() : root.try_as<UIElement>();
    if (!content) return;

    std::wstring out = L"==== dump " + std::to_wstring(GetCurrentProcessId()) + L"\n";
    Dump(content, 0, out);

    auto popups = window ? VisualTreeHelper::GetOpenPopups(window)
                         : content.XamlRoot() ? VisualTreeHelper::GetOpenPopupsForXamlRoot(content.XamlRoot()) : nullptr;
    if (popups)
        for (auto popup : popups) {
            out += L"==== popup\n";
            Dump(popup, 0, out);
        }
    LogRaw(out);
}

// Ferramenta de descoberta (HKCU\Software\TemaBarra\MenusDump = 1): a cada incremento de MenusDumpNow, despeja a árvore
// XAML desta raiz no log. Confere o valor 1x por segundo.
void WatchTreeDump(winrt::Windows::Foundation::IInspectable const& root) {
    DispatcherTimer timer;
    auto last = std::make_shared<DWORD>(Setting(L"MenusDumpNow", 0));

    timer.Interval(std::chrono::seconds(1));
    timer.Tick([root, last](auto&&, auto&&) {
        try {
            DWORD now = Setting(L"MenusDumpNow", 0);
            if (now == *last) return;

            *last = now;
            DumpTree(root);
        } catch (...) {
            Log(L"erro no dump: %08X", winrt::to_hresult());
        }
    });
    timer.Start();
}
