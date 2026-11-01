#include "taskbar/taskbar.h"
#include "common/log.h"
#include "common/xaml_tree.h"
#include "wallpaper/audio.h"
#include "wallpaper/media.h"
#include "wallpaper/timing.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <vector>

namespace Media = winrt::Windows::UI::Xaml::Media;

constexpr int kBars = 37;
constexpr float kBarWidth = 2, kBarPitch = 4, kWaveHeight = 32;
constexpr double kLeft = 12, kCover = 40, kGap = 10, kInfoWidth = 220, kRoomGap = 16;
constexpr double kWaveWidth = kBars * kBarPitch - (kBarPitch - kBarWidth);
constexpr double kShortWidth = kLeft + kCover + kGap + kInfoWidth, kFullWidth = kShortWidth + kGap + kWaveWidth;

struct Player {
    Controls::StackPanel root{nullptr};
    Controls::Border cover{nullptr};
    Controls::Border wave{nullptr};
    Controls::TextBlock title{nullptr}, artist{nullptr}, elapsed{nullptr}, total{nullptr};
    Controls::FontIcon shuffle{nullptr}, play{nullptr};
    Media::ScaleTransform progress{nullptr};
    std::vector<CompositionSpriteShape> bars;
    std::vector<CompositionRoundedRectangleGeometry> barShapes;
    std::shared_ptr<std::vector<BYTE>> shownCover;
    bool shuffleOn = false;
};

// Tudo na thread do XAML da barra (as barras de todos os monitores dividem a mesma).
static thread_local std::vector<std::shared_ptr<Player>> g_players;
static thread_local winrt::Windows::UI::Xaml::Hosting::DesktopWindowXamlSource g_primarySource{nullptr};
static thread_local DispatcherTimer g_stateTimer{nullptr}, g_waveTimer{nullptr};
static thread_local Equalizer g_equalizer;
static thread_local LoopbackCapture g_loopback;
static thread_local double g_lastWave;
static thread_local bool g_dancing;
static std::atomic<bool> g_fullScreen{false};

// Pincel cinza (`gray` de 0 a 255) com opacidade `alpha`.
static Media::SolidColorBrush Gray(BYTE gray, BYTE alpha = 255) {
    return Media::SolidColorBrush(Color{alpha, gray, gray, gray});
}

// Uma linha de texto que termina em "…" quando não cabe.
static Controls::TextBlock Line(double size, BYTE gray) {
    Controls::TextBlock text;
    text.FontSize(size);
    text.Foreground(Gray(gray));
    text.TextTrimming(TextTrimming::CharacterEllipsis);
    text.VerticalAlignment(VerticalAlignment::Center);
    return text;
}

// Grade de uma linha só: 0 é coluna automática, outro número é a parte do que sobra.
static Controls::Grid Row(std::initializer_list<double> columns, double height) {
    Controls::Grid grid;
    grid.Height(height);
    grid.ColumnSpacing(6);
    for (double share : columns) {
        Controls::ColumnDefinition column;
        column.Width(share ? GridLength{share, GridUnitType::Star} : GridLength{1, GridUnitType::Auto});
        grid.ColumnDefinitions().Append(column);
    }
    return grid;
}

// Põe o elemento na coluna `column` da grade.
static void Put(Controls::Grid const& grid, FrameworkElement const& child, int column) {
    Controls::Grid::SetColumn(child, column);
    grid.Children().Append(child);
}

// Botão do player: o ícone (Segoe Fluent Icons) numa placa que clareia no hover, como os botões da barra. O clique
// manda o comando para o Spotify (0 aleatória, 1 voltar, 2 tocar/pausar, 3 avançar) e não chega na barra.
static Controls::FontIcon AddButton(Controls::StackPanel const& row, const wchar_t* glyph, int command) {
    Controls::FontIcon icon;
    icon.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
    icon.Glyph(glyph);
    icon.FontSize(12);
    icon.Foreground(Gray(255));

    Controls::Border button;
    button.Width(22);
    button.Height(20);
    button.CornerRadius({4, 4, 4, 4});
    button.Background(Gray(255, 0));
    button.Child(icon);
    button.PointerEntered([](auto const& sender, auto&&) { sender.as<Controls::Border>().Background(Gray(255, 30)); });
    button.PointerExited([](auto const& sender, auto&&) { sender.as<Controls::Border>().Background(Gray(255, 0)); });
    button.Tapped([command](auto&&, Input::TappedRoutedEventArgs const& args) {
        args.Handled(true);
        SendMediaCommand(command);
    });
    row.Children().Append(button);
    return icon;
}

// Onda da música: uma barra (retângulo arredondado da composição) por coluna, desenhada a cada quadro sem passar pelo
// layout do XAML.
static Controls::Border MakeWave(Player& p) {
    Controls::Border wave;
    wave.Width(kWaveWidth);
    wave.Height(kWaveHeight);
    wave.VerticalAlignment(VerticalAlignment::Center);

    auto c = ElementCompositionPreview::GetElementVisual(wave).Compositor();
    auto shapes = c.CreateShapeVisual();
    shapes.Size({float(kWaveWidth), kWaveHeight});
    auto brush = c.CreateColorBrush({235, 255, 255, 255});
    for (int i = 0; i < kBars; i++) {
        auto geometry = c.CreateRoundedRectangleGeometry();
        geometry.CornerRadius({kBarWidth / 2, kBarWidth / 2});
        auto bar = c.CreateSpriteShape(geometry);
        bar.FillBrush(brush);
        shapes.Shapes().Append(bar);
        p.bars.push_back(bar);
        p.barShapes.push_back(geometry);
    }
    ElementCompositionPreview::SetElementChildVisual(wave, shapes);
    return wave;
}

// O player inteiro, como o cartão do papel de parede: capa; nome da música com os botões (aleatória, voltar,
// tocar/pausar, avançar); artista com o tempo, a barra de progresso e a duração; e a onda ao lado. Começa escondido.
static std::shared_ptr<Player> MakePlayer() {
    auto p = std::make_shared<Player>();

    p->cover = Controls::Border();
    p->cover.Width(kCover);
    p->cover.Height(kCover);
    p->cover.CornerRadius({6, 6, 6, 6});
    p->cover.Background(Gray(38));
    p->cover.VerticalAlignment(VerticalAlignment::Center);

    auto top = Row({1, 0}, 20);
    p->title = Line(13, 255);
    p->title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    Controls::StackPanel buttons;
    buttons.Orientation(Controls::Orientation::Horizontal);
    p->shuffle = AddButton(buttons, L"", 0);
    p->shuffle.Foreground(Gray(120));
    AddButton(buttons, L"", 1);
    p->play = AddButton(buttons, L"", 2);
    AddButton(buttons, L"", 3);
    Put(top, p->title, 0);
    Put(top, buttons, 1);

    auto bottom = Row({0, 0, 1, 0}, 18);
    p->artist = Line(11.5, 179);
    p->artist.MaxWidth(90);
    p->elapsed = Line(11, 154);
    p->total = Line(11, 154);
    Controls::Grid track;
    track.Height(3);
    track.CornerRadius({1.5, 1.5, 1.5, 1.5});
    track.Background(Gray(255, 50));
    track.VerticalAlignment(VerticalAlignment::Center);
    Controls::Border fill;
    fill.CornerRadius({1.5, 1.5, 1.5, 1.5});
    fill.Background(Gray(230));
    p->progress = Media::ScaleTransform();
    p->progress.ScaleX(0);
    fill.RenderTransform(p->progress);
    track.Children().Append(fill);
    Put(bottom, p->artist, 0);
    Put(bottom, p->elapsed, 1);
    Put(bottom, track, 2);
    Put(bottom, p->total, 3);

    Controls::StackPanel info;
    info.Width(kInfoWidth);
    info.VerticalAlignment(VerticalAlignment::Center);
    info.Children().Append(top);
    info.Children().Append(bottom);

    p->wave = MakeWave(*p);
    p->root = Controls::StackPanel();
    p->root.Name(L"TemaPlayer");
    p->root.Orientation(Controls::Orientation::Horizontal);
    p->root.Spacing(kGap);
    p->root.Margin({kLeft, 0, 0, 0});
    p->root.HorizontalAlignment(HorizontalAlignment::Left);
    p->root.VerticalAlignment(VerticalAlignment::Center);
    p->root.Visibility(Visibility::Collapsed);
    p->root.Children().Append(p->cover);
    p->root.Children().Append(info);
    p->root.Children().Append(p->wave);
    return p;
}

// Troca o texto só quando mudou, para não refazer o layout da barra à toa.
static void SetText(Controls::TextBlock const& block, std::wstring const& text) {
    if (block.Text() != text) block.Text(text);
}

// Capa nova no quadrado: os pixels já vêm em BGRA pré-multiplicado, o formato do WriteableBitmap. Sem capa, cinza.
static void ShowCover(Player& p, std::shared_ptr<std::vector<BYTE>> const& pixels) {
    if (pixels == p.shownCover) return;

    p.shownCover = pixels;
    int side = CoverSize();
    if (!pixels || pixels->size() != size_t(side) * side * 4) {
        p.cover.Background(Gray(38));
        return;
    }

    Media::Imaging::WriteableBitmap bitmap(side, side);
    memcpy(bitmap.PixelBuffer().data(), pixels->data(), pixels->size());
    bitmap.Invalidate();
    Media::ImageBrush brush;
    brush.ImageSource(bitmap);
    brush.Stretch(Media::Stretch::UniformToFill);
    p.cover.Background(brush);
}

// A música no player: nome, artista, capa, ícones de aleatória e tocar/pausar, e o tempo andando entre uma leitura do
// Spotify e outra.
static void ShowSong(Player& p, NowPlaying const& song, double now) {
    SetText(p.title, song.title);
    SetText(p.artist, song.artist);
    ShowCover(p, song.cover);

    const wchar_t* glyph = song.playing ? L"" : L"";
    if (p.play.Glyph() != glyph) p.play.Glyph(glyph);
    if (song.shuffle != p.shuffleOn) {
        p.shuffleOn = song.shuffle;
        p.shuffle.Foreground(Gray(song.shuffle ? 255 : 120));
    }

    double position = std::min(song.position + (song.playing ? now - song.readAt : 0), song.duration);
    SetText(p.elapsed, FormatTime(position));
    SetText(p.total, FormatTime(song.duration));
    p.progress.ScaleX(song.duration > 0 ? position / song.duration : 0);
}

// Barras da onda: do meio (graves) para as pontas (agudos), as da direita meia banda deslocadas das da esquerda para
// não ficar um espelho; cada uma cresce do centro para cima e para baixo, como a onda de um áudio. Parada, vira uma
// linha de pontinhos.
static void DrawWave(Player& p) {
    const int middle = kBars / 2;
    for (int i = 0; i < kBars; i++) {
        int distance = std::abs(i - middle);
        int band = std::min(Equalizer::kBands - 1, (distance * (Equalizer::kBands - 1) + (i > middle ? middle / 2 : 0)) / middle);
        float height = 2 * std::round((2 + g_equalizer.level[band] * (kWaveHeight - 2)) / 2);
        p.barShapes[i].Size({kBarWidth, height});
        p.bars[i].Offset({i * kBarPitch, (kWaveHeight - height) / 2});
    }
}

// Jogo ou app em tela cheia? A barra fica escondida atrás dele.
static bool FullScreenApp() {
    QUERY_USER_NOTIFICATION_STATE state;
    return SUCCEEDED(SHQueryUserNotificationState(&state)) &&
           (state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE);
}

// Alguma janela maximizada (visível e não oculta pelo DWM) no monitor principal? É a "tela cheia" em que a área de
// trabalho, e o cartão da música do papel de parede, somem atrás dela.
static bool PrimaryCovered() {
    struct { HMONITOR primary; bool covered; } state{MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), false};

    EnumWindows([](HWND window, LPARAM p) -> BOOL {
        auto s = (decltype(state)*)p;
        BOOL cloaked = FALSE;
        if (!IsWindowVisible(window) || !IsZoomed(window) || MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) != s->primary) return TRUE;

        DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof cloaked);
        s->covered = !cloaked;
        return !s->covered;
    }, (LPARAM)&state);
    return state.covered;
}

// Espaço livre à esquerda dos botões da barra (até o Iniciar, o primeiro deles), em DIPs. Sem achar os botões, sobra
// espaço.
static double FreeWidth(FrameworkElement const& grid) {
    auto repeater = FindDescendant(grid, L"TaskbarFrameRepeater", 4);
    if (!repeater) return 1e9;

    double left = 1e9;
    for (int i = 0, count = VisualTreeHelper::GetChildrenCount(repeater); i < count; i++) {
        auto item = VisualTreeHelper::GetChild(repeater, i).try_as<FrameworkElement>();
        if (!item || item.ActualWidth() <= 0 || item.Visibility() != Visibility::Visible) continue;

        double x = item.TransformToVisual(grid).TransformPoint({0, 0}).X;
        if (x >= 0) left = std::min(left, x);
    }
    return left;
}

// A cada 250 ms: o player aparece na barra do monitor principal quando ele está coberto por uma janela maximizada e o
// Spotify tem música (tocando ou pausada), se couber antes do Iniciar (sem espaço para a onda, só ela sai). O som é
// capturado só com a onda na tela e a música tocando neste PC; tocando em outro aparelho, a onda dança sozinha.
static void Tick() {
    bool fullScreen = FullScreenApp();
    g_fullScreen = fullScreen;
    NowPlaying song = CurrentMedia();
    auto content = g_primarySource ? g_primarySource.Content() : nullptr;
    auto primary = content ? content.XamlRoot() : nullptr;
    bool show = !fullScreen && !song.title.empty() && primary && PrimaryCovered();
    double now = Now();
    bool waving = false;

    std::erase_if(g_players, [](auto const& p) { return !VisualTreeHelper::GetParent(p->root); });
    for (auto const& p : g_players) {
        double room = show && p->root.XamlRoot() == primary ? FreeWidth(ParentOf(p->root)) - kRoomGap : 0;
        p->wave.Visibility(room >= kFullWidth ? Visibility::Visible : Visibility::Collapsed);
        p->root.Visibility(room >= kShortWidth ? Visibility::Visible : Visibility::Collapsed);
        if (room >= kShortWidth) ShowSong(*p, song, now);
        waving |= room >= kFullWidth;
    }

    g_dancing = song.playing && song.elsewhere;
    g_loopback.Want(waving && song.playing && !song.elsewhere, GetTickCount64());
    if (waving && !g_waveTimer.IsEnabled()) {
        g_lastWave = now;
        g_waveTimer.Start();
    } else if (!waving) {
        g_waveTimer.Stop();
    }
}

// A cada quadro da onda (~30 por segundo): lê o som, atualiza o equalizador e redesenha as barras visíveis.
static void WaveTick() {
    double now = Now();
    float dt = std::min(float(now - g_lastWave), 0.1f);
    g_lastWave = now;

    g_equalizer.Read(g_loopback.capture.Get());
    if (g_dancing) g_equalizer.Simulate(now, dt);
    else g_equalizer.Update(g_loopback.capture != nullptr, dt);
    for (auto const& p : g_players)
        if (p->wave.Visibility() == Visibility::Visible && p->root.Visibility() == Visibility::Visible) DrawWave(*p);
}

// Liga a leitura do Spotify (parada com a barra atrás de um jogo em tela cheia) e os dois timers, uma vez só.
static void StartPlayerTimers() {
    if (g_stateTimer) return;

    StartMediaWatcher([] { return g_fullScreen.load(); });
    g_stateTimer = DispatcherTimer();
    g_stateTimer.Interval(std::chrono::milliseconds(250));
    g_stateTimer.Tick([](auto&&, auto&&) {
        try {
            Tick();
        } catch (...) {
            Log(L"erro no player: %08X", winrt::to_hresult());
        }
    });
    g_stateTimer.Start();

    g_waveTimer = DispatcherTimer();
    g_waveTimer.Interval(std::chrono::milliseconds(33));
    g_waveTimer.Tick([](auto&&, auto&&) {
        try {
            WaveTick();
        } catch (...) {
            Log(L"erro na onda do player: %08X", winrt::to_hresult());
        }
    });
}

// Põe o player (escondido) no Grid de cima da barra, por cima do fundo e à esquerda dos botões, uma vez por barra.
void AddMediaPlayer(FrameworkElement const& frame) {
    auto found = FindDescendant(frame, L"RootGrid", 3);
    auto grid = found ? found.try_as<Controls::Grid>() : nullptr;
    if (!grid || FindDescendant(grid, L"TemaPlayer", 1)) return;

    auto player = MakePlayer();
    Controls::Grid::SetColumnSpan(player->root, std::max(1, int(grid.ColumnDefinitions().Size())));
    Controls::Grid::SetRowSpan(player->root, std::max(1, int(grid.RowDefinitions().Size())));
    grid.Children().Append(player->root);
    g_players.push_back(player);
    StartPlayerTimers();

    std::wstring children;
    for (int i = 0, count = VisualTreeHelper::GetChildrenCount(grid); i < count; i++) {
        auto child = VisualTreeHelper::GetChild(grid, i).try_as<FrameworkElement>();
        children += ClassOf(child) + L"#" + std::wstring(child ? child.Name() : L"") + L" ";
    }
    Log(L"player do Spotify na barra; RootGrid: %s", children.c_str());
}

// Cada barra é uma ilha XAML (DesktopWindowXamlSource) numa janela própria: a do monitor principal fica dentro da
// Shell_TrayWnd, as outras na Shell_SecondaryTrayWnd. Guarda a da principal, onde o player aparece.
void MarkPrimaryTaskbar(winrt::Windows::Foundation::IInspectable const& object) {
    auto source = object.try_as<winrt::Windows::UI::Xaml::Hosting::DesktopWindowXamlSource>();
    auto native = object.try_as<IDesktopWindowXamlSourceNative>();
    HWND island = nullptr;
    wchar_t owner[64] = {};
    if (!source || !native || FAILED(native->get_WindowHandle(&island)) || !island) return;

    GetClassNameW(GetAncestor(island, GA_ROOT), owner, 64);
    Log(L"ilha XAML da barra em %s", owner);
    if (!wcscmp(owner, L"Shell_TrayWnd")) g_primarySource = source;
}
