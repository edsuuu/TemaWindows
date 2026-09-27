#include "menus/menus.h"
#include "common/log.h"
#include "common/xaml_tree.h"

#include <winrt/Windows.UI.Core.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <tuple>

winrt::weak_ref<FrameworkElement> g_avatarSlot;
winrt::weak_ref<FrameworkElement> g_powerSlot;

struct NativeFooter {
    FrameworkElement user{nullptr};
    FrameworkElement photo{nullptr};
    FrameworkElement power{nullptr};
    FrameworkElement apps{nullptr};
    bool logged = false;
    ULONGLONG lastAppsSearch = 0;
};

// Rola a lista do Iniciar para o topo ou até o cabeçalho "Pastas de apps". Mexe só no ScrollViewer que rola o conteúdo
// (o mais próximo acima do cabeçalho), nunca no do SemanticZoom de fora: rolar aquele escondia os apps. Clicar N vezes
// dá o mesmo resultado que uma.
static void ScrollContent(FrameworkElement const& menu, bool toApps) {
    auto heading = FindDescendant(menu, L"AllListHeading");
    Controls::ScrollViewer scroller{nullptr};
    for (DependencyObject o = heading ? VisualTreeHelper::GetParent(heading) : nullptr; o && !scroller; o = VisualTreeHelper::GetParent(o))
        scroller = o.try_as<Controls::ScrollViewer>();
    if (!scroller) return;

    double y = 0;
    if (toApps)
        if (auto content = scroller.Content().try_as<UIElement>()) y = heading.TransformToVisual(content).TransformPoint({0, 0}).Y;

    scroller.ChangeView(nullptr, std::max(0.0, y), nullptr);
}

// Leva um elemento NATIVO do rodapé (com os handlers e menus do Windows) para o centro de `slot` na barra, só na
// renderização: TranslateTransform no próprio elemento não mexe no layout (não realimenta o LayoutUpdated) e o clique
// e o menu nativo acompanham a posição desenhada. `anchor` é o que fica centrado (a foto dentro do botão). Se o
// elemento já tem transformação do shell (animação), não mexe e devolve false.
static bool Relocate(FrameworkElement const& mover, FrameworkElement const& anchor, FrameworkElement const& slot, UIElement const& root) {
    if (!mover || !anchor || !slot || anchor.ActualWidth() == 0 || slot.ActualWidth() == 0) return false;

    auto current = mover.RenderTransform();
    auto translate = current.try_as<Media::TranslateTransform>();
    if (!translate) {
        auto matrix = current.try_as<Media::MatrixTransform>();
        if (current && !(matrix && Media::MatrixHelper::GetIsIdentity(matrix.Matrix()))) return false;

        translate = Media::TranslateTransform();
        mover.RenderTransform(translate);
    }

    auto from = anchor.TransformToVisual(root).TransformPoint({0, 0});
    auto to = slot.TransformToVisual(root).TransformPoint({0, 0});
    double dx = to.X + (slot.ActualWidth() - anchor.ActualWidth()) / 2 - from.X;
    double dy = to.Y + (slot.ActualHeight() - anchor.ActualHeight()) / 2 - from.Y;

    if (std::abs(dx) > .5) translate.X(std::round(translate.X() + dx));
    if (std::abs(dy) > .5) translate.Y(std::round(translate.Y() + dy));
    return true;
}

// Painel da barra: largura fixa, à esquerda, com vão até as beiradas, um tico mais claro que o conteúdo (~RGB 20,20,20
// quase opaco) e quatro linhas (foto, navegação, espaço, rodapé).
static Controls::Grid CreateRailPanel() {
    Controls::Grid rail;
    rail.Name(L"ThemeSideRail");
    rail.Width(kRailWidth);
    rail.HorizontalAlignment(HorizontalAlignment::Left);
    rail.Margin({kPanelGap, kPanelGap, 0, kPanelGap});
    rail.Background(Solid(0xEC, 0x14));
    rail.CornerRadius({14, 14, 14, 14});
    rail.BorderBrush(Solid(0x14));
    rail.BorderThickness({1, 1, 1, 1});

    for (auto height : {GridLengthHelper::Auto(), GridLengthHelper::Auto(), GridLengthHelper::FromValueAndType(1, GridUnitType::Star),
                        GridLengthHelper::Auto()}) {
        Controls::RowDefinition row;
        row.Height(height);
        rail.RowDefinitions().Append(row);
    }
    return rail;
}

// Lugar vazio de tamanho fixo, centrado, para onde um botão nativo do rodapé é trazido.
static Controls::Border CreateSlot(double size, Thickness margin) {
    Controls::Border slot;
    slot.Width(size);
    slot.Height(size);
    slot.Margin(margin);
    slot.HorizontalAlignment(HorizontalAlignment::Center);
    return slot;
}

// "Criar": atalhos que abrem ali mesmo na barra (um menu flutuante fechava sozinho, porque o foco volta para a caixa de
// pesquisa). Começa fechado e fecha depois de abrir um app.
static Controls::StackPanel CreateCreateShortcuts() {
    Controls::StackPanel shortcuts;
    shortcuts.Spacing(2);
    shortcuts.Visibility(Visibility::Collapsed);

    for (auto [name, glyph, app] : {std::tuple{L"Bloco de notas", 0xE70B, L"Microsoft.WindowsNotepad_8wekyb3d8bbwe!App"},
                                    {L"Paint", 0xE790, L"Microsoft.Paint_8wekyb3d8bbwe!App"},
                                    {L"Ferramenta de Captura", 0xF406, L"Microsoft.ScreenSketch_8wekyb3d8bbwe!App"}}) {
        std::wstring target = std::wstring(L"shell:AppsFolder\\") + app;
        shortcuts.Children().Append(SideButton((wchar_t)glyph, L"", name, false, [target, shortcuts] {
            shortcuts.Visibility(Visibility::Collapsed);
            Launch(target);
        }));
    }
    return shortcuts;
}

// Fecha os atalhos de "Criar" sempre que o Iniciar some, para abrirem fechados da próxima vez.
static void CollapseWhenHidden(Controls::StackPanel const& shortcuts) {
    try {
        if (auto window = Window::Current())
            window.VisibilityChanged([shortcuts](auto&&, winrt::Windows::UI::Core::VisibilityChangedEventArgs const& args) {
                try { if (!args.Visible()) shortcuts.Visibility(Visibility::Collapsed); } catch (...) {}
            });
    } catch (...) {}
}

// Navegação: Início (topo da lista), Apps (pastas de apps) e Criar (abre os atalhos embaixo dele).
static Controls::StackPanel CreateNavigation(Controls::Grid const& menu) {
    Controls::StackPanel navigation;
    navigation.Spacing(6);
    Controls::Grid::SetRow(navigation, 1);

    navigation.Children().Append(SideButton(0xE80F, L"Início", L"Início", true, [menu] { ScrollContent(menu, false); }));
    navigation.Children().Append(SideButton(0xE71D, L"Apps", L"Todos os apps", false, [menu] { ScrollContent(menu, true); }));

    auto shortcuts = CreateCreateShortcuts();
    navigation.Children().Append(SideButton(0xE70F, L"Criar", L"Criar", false, [shortcuts] {
        shortcuts.Visibility(shortcuts.Visibility() == Visibility::Visible ? Visibility::Collapsed : Visibility::Visible);
    }));
    navigation.Children().Append(shortcuts);
    CollapseWhenHidden(shortcuts);
    return navigation;
}

// Rodapé da barra: Explorador de Arquivos, Configurações e o lugar do botão NATIVO de energia (menu
// Suspender/Desligar/Reiniciar do Windows).
static Controls::StackPanel CreateRailFooter(Controls::Border const& powerSlot) {
    Controls::StackPanel footer;
    footer.Spacing(2);
    footer.Margin({0, 0, 0, 14});
    Controls::Grid::SetRow(footer, 3);

    footer.Children().Append(SideButton(0xE8B7, L"", L"Explorador de Arquivos", false, [] { Launch(L"explorer.exe"); }));
    footer.Children().Append(SideButton(0xE713, L"", L"Configurações", false, [] { Launch(L"ms-settings:"); }));
    footer.Children().Append(powerSlot);
    return footer;
}

// Põe a barra abaixo do MainContent (os botões nativos, que moram nele, ficam por cima e clicáveis). O MainContent tem
// recorte do tamanho dele, então continua na largura toda e quem anda para a direita são os filhos (pesquisa, véu,
// lista); o rodapé nativo fica onde está e a lista desce até o fim, cobrindo a faixa dele.
static void InsertRail(Controls::Grid const& menu, Controls::Grid const& rail) {
    auto children = menu.Children();
    auto mainContent = FindDescendant(menu, L"MainContent", 2);
    uint32_t index = 0;
    if (mainContent && children.IndexOf(mainContent, index)) children.InsertAt(index, rail);
    else children.Append(rail);

    if (auto panel = mainContent.try_as<Controls::Panel>())
        for (auto child : panel.Children())
            if (auto element = child.try_as<FrameworkElement>(); element && element.Name() != L"NavPanePlaceholder") {
                auto margin = element.Margin();
                margin.Left += 2 * kPanelGap + kRailWidth - 1;
                KeepValue(element, FrameworkElement::MarginProperty(), winrt::box_value(margin), kMaxFights);
            }

    if (auto frame = FindDescendant(menu, L"StartFrame", 4)) KeepValue(frame, Controls::Grid::RowSpanProperty(), winrt::box_value(2), kMaxFights);
}

// Onde estão os recortes acima do botão da conta (diagnóstico da primeira vez que os botões nativos vão para a barra).
static std::wstring ClipsAbove(FrameworkElement const& element, UIElement const& menu) {
    std::wstring clips;
    for (DependencyObject o = element; o && o != menu; o = VisualTreeHelper::GetParent(o))
        if (auto ui = o.try_as<UIElement>(); ui && ui.Clip()) clips += ClassOf(o) + L"#" + std::wstring(o.as<FrameworkElement>().Name()) + L" ";
    return clips;
}

// A cada layout: acha os botões nativos do rodapé (conta, foto, energia), procura a grade de apps no máximo 1x por
// segundo para as pastas ocuparem a largura toda, e leva conta e energia para os lugares deles na barra.
static void FollowNativeFooter(Controls::Grid const& menu, FrameworkElement const& avatarSlot, FrameworkElement const& powerSlot,
                               NativeFooter& footer) {
    if (!footer.user || !footer.power)
        if (auto placeholder = FindDescendant(menu, L"NavPanePlaceholder", 3)) {
            footer.user = FindDescendant(placeholder, L"UserTile", 5);
            footer.photo = FindDescendant(placeholder, L"UserTileIcon", 9);
            footer.power = FindDescendant(placeholder, L"PowerButton", 5);
        }

    if ((!footer.apps || !footer.apps.IsLoaded()) && GetTickCount64() - footer.lastAppsSearch > 1000) {
        footer.lastAppsSearch = GetTickCount64();
        footer.apps = FindDescendant(menu, L"AllAppsGrid");
    }
    if (footer.apps) FillFolders(footer.apps);

    bool userMoved = Relocate(footer.user, footer.photo, avatarSlot, menu);
    bool powerMoved = Relocate(footer.power, footer.power, powerSlot, menu);
    if (footer.logged || !footer.user || !footer.power) return;

    footer.logged = true;
    Log(L"rodapé nativo na lateral: conta=%d energia=%d; clip em: %s", userMoved, powerMoved, ClipsAbove(footer.user, menu).c_str());
}

// Barra lateral do conceito "Windows 12". O Iniciar de verdade continua lá (pesquisa, fixados, recomendados, todos);
// só ganha a barra e o conteúdo anda para a direita. Conta e energia são os botões nativos do rodapé trazidos para a
// barra (os menus deles continuam os do Windows e abrem no lugar certo).
static void BuildSideRail(Controls::Grid const& menu) {
    for (auto child : menu.Children())
        if (auto element = child.try_as<FrameworkElement>(); element && element.Name() == L"ThemeSideRail") return;

    auto rail = CreateRailPanel();
    auto avatarSlot = CreateSlot(46, {0, 16, 0, 14});
    auto powerSlot = CreateSlot(44, {});
    rail.Children().Append(avatarSlot);
    g_avatarSlot = winrt::make_weak(avatarSlot.as<FrameworkElement>());

    rail.Children().Append(CreateNavigation(menu));
    rail.Children().Append(CreateRailFooter(powerSlot));
    g_powerSlot = winrt::make_weak(powerSlot.as<FrameworkElement>());
    InsertRail(menu, rail);

    auto footer = std::make_shared<NativeFooter>();
    rail.LayoutUpdated([menu, avatarSlot, powerSlot, footer](auto&&, auto&&) {
        try { FollowNativeFooter(menu, avatarSlot, powerSlot, *footer); } catch (...) {}
    });
    Log(L"barra lateral criada");
}

// Tenta montar a barra; só dá certo quando o MainContent já existe (antes disso ela sumia ou ficava coberta na animação
// de abertura).
static bool TryBuildSideRail(Controls::Grid const& menu) {
    try {
        if (!FindDescendant(menu, L"MainContent", 2)) return false;

        BuildSideRail(menu);
        return true;
    } catch (...) {
        Log(L"erro na barra lateral: %08X", winrt::to_hresult());
        return false;
    }
}

// Monta a barra agora ou, se o MainContent ainda não existe, tenta de novo a cada 60 ms por até ~2,4 s.
void BuildSideRailWhenReady(Controls::Grid const& menu) {
    if (TryBuildSideRail(menu)) return;

    auto timer = std::make_shared<DispatcherTimer>();
    auto ticks = std::make_shared<int>(0);
    timer->Interval(std::chrono::milliseconds(60));
    timer->Tick([timer, menu, ticks](auto&&, auto&&) {
        if (TryBuildSideRail(menu) || ++*ticks > 40) timer->Stop();
    });
    timer->Start();
}
