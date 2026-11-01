#include "taskbar/taskbar.h"
#include "common/log.h"
#include "common/registry.h"
#include "common/xaml_tree.h"
#include "wallpaper/audio.h"
#include "wallpaper/fullscreen.h"
#include "wallpaper/media.h"
#include "wallpaper/palette.h"
#include "wallpaper/timing.h"

#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

namespace Media = winrt::Windows::UI::Xaml::Media;
using winrt::Windows::UI::Xaml::Hosting::DesktopWindowXamlSource;

// Estilos da onda (PlayerOnda): 0 barrinhas que saem do centro, do meio (graves) para as pontas (agudos); 1 uma barra
// por banda, dos graves aos agudos, crescendo de baixo para cima como no cartão do papel de parede.
struct WaveStyle {
    int bars;
    float width, pitch;
    bool fromCenter;
};

constexpr WaveStyle kWaveStyles[2] = {{37, 2, 4, true}, {Equalizer::kBands, 3, 5, false}};
constexpr float kWaveHeight = 32;
constexpr double kLeft = 12, kCover = 40, kGap = 10, kInfoWidth = 220, kRoomGap = 16;
constexpr double kShortWidth = kLeft + kCover + kGap + kInfoWidth;

constexpr float WaveWidth(WaveStyle const& style) {
    return style.bars * style.pitch - (style.pitch - style.width);
}

struct Player {
    Controls::StackPanel root{nullptr};
    Controls::Border cover{nullptr}, wave{nullptr};
    Controls::Grid track{nullptr};
    Controls::TextBlock title{nullptr}, artist{nullptr}, elapsed{nullptr}, total{nullptr};
    Controls::FontIcon shuffle{nullptr}, play{nullptr};
    Media::ScaleTransform progress{nullptr};
    std::vector<CompositionSpriteShape> bars;
    std::vector<CompositionRoundedRectangleGeometry> barShapes;
    std::vector<CompositionColorBrush> barColors;
    std::shared_ptr<std::vector<BYTE>> shownCover;
    bool shuffleOn = false, gone = false;
};

// Tudo na thread do XAML da barra (as barras de todos os monitores dividem a mesma).
static thread_local std::vector<std::shared_ptr<Player>> g_players;
static thread_local std::vector<DesktopWindowXamlSource> g_islands;
static thread_local DispatcherTimer g_stateTimer{nullptr}, g_waveTimer{nullptr};
static thread_local Equalizer g_equalizer;
static thread_local LoopbackCapture g_loopback;
static thread_local double g_lastWave;
static thread_local bool g_dancing, g_enabled = true;
static thread_local int g_style = -1, g_palette = -1;
static std::atomic<bool> g_idle{true};

static Color ToColor(D2D1_COLOR_F const& c) {
    return {BYTE(c.a * 255 + 0.5f), BYTE(c.r * 255 + 0.5f), BYTE(c.g * 255 + 0.5f), BYTE(c.b * 255 + 0.5f)};
}

// Pincel na cor do tema (as cores do papel de parede, escolhidas no app do Wallget) com o brilho `gray` e a opacidade
// `alpha`, os mesmos do cartão da música.
static Media::SolidColorBrush Brush(Ink ink, float gray, float alpha) {
    return Media::SolidColorBrush(ToColor(Paint(ink, gray, alpha)));
}

// Quadrado cinza no lugar da capa, enquanto ela não chega.
static Media::SolidColorBrush NoCover() {
    return Media::SolidColorBrush(Color{255, 38, 38, 38});
}

// Uma linha de texto que termina em "…" quando não cabe.
static Controls::TextBlock Line(double size, Media::Brush const& color) {
    Controls::TextBlock text;
    text.FontSize(size);
    text.Foreground(color);
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

// Tira o elemento da grade.
static void Remove(Controls::Grid const& grid, UIElement const& child) {
    uint32_t index;
    if (grid.Children().IndexOf(child, index)) grid.Children().RemoveAt(index);
}

// Botão do player: o ícone (Segoe Fluent Icons) num Button sem borda nem fundo, que clareia no hover como os botões da
// barra e tem nome para o leitor de tela. O clique (ou Enter/Espaço) manda o comando para o Spotify (0 aleatória, 1
// voltar, 2 tocar/pausar, 3 avançar). Sem o anel de foco, como os outros botões da barra.
static Controls::FontIcon AddButton(Controls::StackPanel const& row, const wchar_t* glyph, const wchar_t* name, int command) {
    Controls::FontIcon icon;
    icon.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
    icon.Glyph(glyph);
    icon.FontSize(12);
    icon.Foreground(Brush(Ink::Text, 0.92f, 0.95f));

    Controls::Button button;
    button.Width(22);
    button.Height(20);
    button.Padding({0, 0, 0, 0});
    button.BorderThickness({0, 0, 0, 0});
    button.CornerRadius({4, 4, 4, 4});
    button.Background(Media::SolidColorBrush(Color{0, 255, 255, 255}));
    button.UseSystemFocusVisuals(false);
    button.Content(icon);
    Automation::AutomationProperties::SetName(button, name);
    button.Click([command](auto&&, auto&&) { SendMediaCommand(command); });
    row.Children().Append(button);
    return icon;
}

// Onda da música: uma barra (retângulo da composição, com a própria cor) por coluna, desenhada a cada quadro sem passar
// pelo layout do XAML.
static Controls::Border MakeWave(Player& p, WaveStyle const& style) {
    Controls::Border wave;
    wave.Width(WaveWidth(style));
    wave.Height(kWaveHeight);
    wave.VerticalAlignment(VerticalAlignment::Center);

    auto c = ElementCompositionPreview::GetElementVisual(wave).Compositor();
    auto shapes = c.CreateShapeVisual();
    shapes.Size({WaveWidth(style), kWaveHeight});
    for (int i = 0; i < style.bars; i++) {
        auto geometry = c.CreateRoundedRectangleGeometry();
        if (style.fromCenter) geometry.CornerRadius({style.width / 2, style.width / 2});
        auto color = c.CreateColorBrush();
        auto bar = c.CreateSpriteShape(geometry);
        bar.FillBrush(color);
        shapes.Shapes().Append(bar);
        p.bars.push_back(bar);
        p.barShapes.push_back(geometry);
        p.barColors.push_back(color);
    }
    ElementCompositionPreview::SetElementChildVisual(wave, shapes);
    return wave;
}

// O player inteiro, como o cartão do papel de parede e nas cores dele: capa; nome da música com os botões (aleatória,
// voltar, tocar/pausar, avançar); artista com o tempo, a barra de progresso e a duração; e a onda ao lado, no estilo
// escolhido. Começa escondido e marca quando sai da árvore (a barra foi recriada), para sair da lista.
static std::shared_ptr<Player> MakePlayer() {
    auto p = std::make_shared<Player>();

    p->cover = Controls::Border();
    p->cover.Width(kCover);
    p->cover.Height(kCover);
    p->cover.CornerRadius({6, 6, 6, 6});
    p->cover.Background(NoCover());
    p->cover.VerticalAlignment(VerticalAlignment::Center);

    auto top = Row({1, 0}, 20);
    p->title = Line(13, Brush(Ink::Text, 0.95f, 0.97f));
    p->title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    Controls::StackPanel buttons;
    buttons.Orientation(Controls::Orientation::Horizontal);
    p->shuffle = AddButton(buttons, L"", L"Aleatória", 0);
    p->shuffle.Foreground(Brush(Ink::Text, 0.92f, 0.4f));
    AddButton(buttons, L"", L"Anterior", 1);
    p->play = AddButton(buttons, L"", L"Tocar ou pausar", 2);
    AddButton(buttons, L"", L"Próxima", 3);
    Put(top, p->title, 0);
    Put(top, buttons, 1);

    auto bottom = Row({0, 0, 1, 0}, 18);
    p->artist = Line(11.5, Brush(Ink::Soft, 0.78f, 0.95f));
    p->artist.MaxWidth(90);
    p->elapsed = Line(11, Brush(Ink::Soft, 0.62f, 0.95f));
    p->total = Line(11, Brush(Ink::Soft, 0.62f, 0.95f));
    p->track = Controls::Grid();
    p->track.Height(3);
    p->track.CornerRadius({1.5, 1.5, 1.5, 1.5});
    p->track.Background(Brush(Ink::Accent, 1, 0.16f));
    p->track.VerticalAlignment(VerticalAlignment::Center);
    Controls::Border fill;
    fill.CornerRadius({1.5, 1.5, 1.5, 1.5});
    fill.Background(Brush(Ink::Accent, 0.9f, 0.9f));
    p->progress = Media::ScaleTransform();
    p->progress.ScaleX(0);
    fill.RenderTransform(p->progress);
    p->track.Children().Append(fill);
    Put(bottom, p->artist, 0);
    Put(bottom, p->elapsed, 1);
    Put(bottom, p->track, 2);
    Put(bottom, p->total, 3);

    Controls::StackPanel info;
    info.Width(kInfoWidth);
    info.VerticalAlignment(VerticalAlignment::Center);
    info.Children().Append(top);
    info.Children().Append(bottom);

    p->wave = MakeWave(*p, kWaveStyles[g_style]);
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

    std::weak_ptr<Player> weak = p;
    p->root.Loaded([weak](auto&&, auto&&) {
        if (auto p = weak.lock()) p->gone = false;
    });
    p->root.Unloaded([weak](auto&&, auto&&) {
        if (auto p = weak.lock()) p->gone = true;
    });
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
        p.cover.Background(NoCover());
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
// Spotify e outra (no máximo 2 s além da última, como no cartão). Sem duração (anúncio, podcast ao vivo), sem tempo.
static void ShowSong(Player& p, NowPlaying const& song, double now) {
    SetText(p.title, song.title);
    SetText(p.artist, song.artist);
    ShowCover(p, song.cover);

    const wchar_t* glyph = song.playing ? L"" : L"";
    if (p.play.Glyph() != glyph) p.play.Glyph(glyph);
    if (song.shuffle != p.shuffleOn) {
        p.shuffleOn = song.shuffle;
        p.shuffle.Foreground(Brush(Ink::Text, 0.92f, song.shuffle ? 0.95f : 0.4f));
    }

    auto timed = song.duration > 0 ? Visibility::Visible : Visibility::Collapsed;
    p.elapsed.Visibility(timed);
    p.track.Visibility(timed);
    p.total.Visibility(timed);
    if (song.duration <= 0) return;

    double position = std::clamp(song.position + (song.playing ? std::min(now - song.readAt, 2.0) : 0), 0.0, song.duration);
    SetText(p.elapsed, FormatTime(position));
    SetText(p.total, FormatTime(song.duration));
    p.progress.ScaleX(position / song.duration);
}

// Barras da onda no estilo escolhido, na cor do equalizador do tema, indo para a do pico (vermelha) quando a banda
// passa de 85%, como no cartão do papel de parede. Do centro: as da direita meia banda deslocadas das da esquerda para
// não ficar um espelho, cada uma crescendo para cima e para baixo; parada, vira uma linha de pontinhos. De baixo para
// cima: uma barra por banda, dos graves aos agudos.
static void DrawWave(Player& p, WaveStyle const& style) {
    const int middle = style.bars / 2;
    for (int i = 0; i < style.bars; i++) {
        int distance = std::abs(i - middle);
        int band = style.fromCenter ? std::min(Equalizer::kBands - 1, (distance * (Equalizer::kBands - 1) + (i > middle ? middle / 2 : 0)) / middle) : i;
        float level = g_equalizer.level[band], height = 2 * std::round((2 + level * (kWaveHeight - 2)) / 2);
        float red = std::clamp((level - 0.85f) / 0.15f, 0.f, 1.f);
        D2D1_COLOR_F calm = Paint(Ink::Equalizer, 0.9f, 0.6f + 0.4f * level), peak = Paint(Ink::Peak, 0.95f, calm.a);
        p.barShapes[i].Size({style.width, height});
        p.bars[i].Offset({i * style.pitch, style.fromCenter ? (kWaveHeight - height) / 2 : kWaveHeight - height});
        p.barColors[i].Color(ToColor({calm.r + (peak.r - calm.r) * red, calm.g + (peak.g - calm.g) * red, calm.b + (peak.b - calm.b) * red, calm.a}));
    }
}

// XamlRoot da barra do monitor principal. Cada barra é uma ilha XAML numa janela própria: a do principal fica dentro
// da Shell_TrayWnd, as outras na Shell_SecondaryTrayWnd. A ilha só ganha janela depois de entrar na barra, por isso a
// procura é a cada volta; ilha fechada ou com a janela destruída (a barra foi recriada) sai da lista.
static winrt::Windows::UI::Xaml::XamlRoot PrimaryRoot() {
    winrt::Windows::UI::Xaml::XamlRoot primary{nullptr};
    std::erase_if(g_islands, [&](DesktopWindowXamlSource const& island) {
        try {
            auto content = island.Content();
            HWND window = nullptr;
            wchar_t owner[64] = {};
            if (FAILED(island.as<IDesktopWindowXamlSourceNative>()->get_WindowHandle(&window)) || !window) return false;
            if (!IsWindow(window)) return true;

            GetClassNameW(GetAncestor(window, GA_ROOT), owner, 64);
            if (content && !wcscmp(owner, L"Shell_TrayWnd")) primary = content.XamlRoot();
            return false;
        } catch (...) {
            return true;
        }
    });
    return primary;
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

// A cada 250 ms: o player aparece na barra do monitor principal sempre que o Spotify tem música, tocando ou pausada
// (pausada, a onda vira a linha parada), se couber antes do Iniciar (sem espaço para a onda, só ela sai). Sem poder
// aparecer (Player = 0, jogo em tela cheia) nem lê o Spotify. O som é capturado só com a onda na tela e a música
// tocando neste PC; tocando em outro aparelho, a onda dança sozinha.
static void Tick() {
    auto primary = PrimaryRoot();
    bool idle = !g_enabled || !primary || FullScreenApp();
    g_idle = idle;
    NowPlaying song = CurrentMedia();
    bool show = !idle && !song.title.empty();
    double now = Now(), full = kShortWidth + kGap + WaveWidth(kWaveStyles[g_style]);
    bool waving = false;

    std::erase_if(g_players, [](auto const& p) {
        if (p->gone) p->root.Visibility(Visibility::Collapsed);
        return p->gone;
    });
    for (auto const& p : g_players) {
        double room = show && p->root.XamlRoot() == primary ? FreeWidth(ParentOf(p->root)) - kRoomGap : 0;
        p->wave.Visibility(room >= full ? Visibility::Visible : Visibility::Collapsed);
        p->root.Visibility(room >= kShortWidth ? Visibility::Visible : Visibility::Collapsed);
        if (room >= kShortWidth) ShowSong(*p, song, now);
        waving |= room >= full;
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
        if (p->wave.Visibility() == Visibility::Visible && p->root.Visibility() == Visibility::Visible) DrawWave(*p, kWaveStyles[g_style]);
}

// Roda `step` sem deixar exceção escapar (derrubaria o Explorer). O erro vai para o log só quando muda, não a cada
// volta do timer.
static void Guarded(const wchar_t* what, HRESULT& last, void (*step)()) {
    try {
        step();
        last = S_OK;
    } catch (...) {
        HRESULT error = winrt::to_hresult();
        if (error != last) Log(L"erro %s: %08X", what, error);
        last = error;
    }
}

// Liga a leitura do Spotify (parada enquanto o player não pode aparecer) e os dois timers, uma vez só.
static void StartPlayerTimers() {
    if (g_stateTimer) return;

    StartMediaWatcher([] { return g_idle.load(); });
    g_stateTimer = DispatcherTimer();
    g_stateTimer.Interval(std::chrono::milliseconds(250));
    g_stateTimer.Tick([](auto&&, auto&&) {
        thread_local HRESULT last = S_OK;
        Guarded(L"no player", last, Tick);
    });
    g_stateTimer.Start();

    g_waveTimer = DispatcherTimer();
    g_waveTimer.Interval(std::chrono::milliseconds(33));
    g_waveTimer.Tick([](auto&&, auto&&) {
        thread_local HRESULT last = S_OK;
        Guarded(L"na onda do player", last, WaveTick);
    });
}

// Põe um player novo (escondido) na grade, ocupando ela toda.
static void PutPlayer(Controls::Grid const& grid) {
    auto player = MakePlayer();
    Controls::Grid::SetColumnSpan(player->root, std::max(1, int(grid.ColumnDefinitions().Size())));
    Controls::Grid::SetRowSpan(player->root, std::max(1, int(grid.RowDefinitions().Size())));
    grid.Children().Append(player->root);
    g_players.push_back(player);
}

// A cada segundo, junto com o resto da barra: relê Player (0 esconde), PlayerOnda (o estilo da onda) e as cores do
// tema. Mudou o estilo ou alguma cor, troca cada player por um novo com o visual novo.
void RefreshMediaPlayer() {
    int style = Setting(L"PlayerOnda", 0) == 1;
    g_enabled = Enabled(L"Player");
    ReloadPalette();
    if (style == g_style && PaletteVersion() == g_palette) return;

    g_style = style;
    g_palette = PaletteVersion();
    for (auto const& p : std::exchange(g_players, {})) {
        auto grid = ParentOf(p->root).try_as<Controls::Grid>();
        if (!grid) continue;

        Remove(grid, p->root);
        PutPlayer(grid);
    }
}

// Põe o player no Grid de cima da barra, por cima do fundo e à esquerda dos botões, uma vez por barra. Um player que
// ficou para trás (saiu da lista quando a barra saiu da árvore, e ela voltou) é trocado por um novo.
void AddMediaPlayer(FrameworkElement const& frame) {
    auto found = FindDescendant(frame, L"RootGrid", 3);
    auto grid = found ? found.try_as<Controls::Grid>() : nullptr;
    if (!grid) return;

    RefreshMediaPlayer();
    if (auto old = FindDescendant(grid, L"TemaPlayer", 1)) {
        if (std::any_of(g_players.begin(), g_players.end(), [&](auto const& p) { return p->root == old; })) return;
        Remove(grid, old);
    }
    PutPlayer(grid);
    StartPlayerTimers();
}

// Guarda a ilha XAML (DesktopWindowXamlSource) de cada barra, para achar a do monitor principal, onde o player
// aparece.
void AddTaskbarIsland(winrt::Windows::Foundation::IInspectable const& object) {
    if (auto island = object.try_as<DesktopWindowXamlSource>()) g_islands.push_back(island);
}
