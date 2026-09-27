#include "menus/menus.h"
#include "common/log.h"

#include <winrt/Windows.Media.Control.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>

using namespace winrt::Windows::Media::Control;
using TimeSpan = winrt::Windows::Foundation::TimeSpan;

constexpr double kTimelineHeight = 33;

struct Timeline {
    Controls::Grid root;
    Controls::Border track;
    Controls::Border fill;
    Controls::TextBlock elapsed;
    Controls::TextBlock duration;
    FrameworkElement card{nullptr};
    double cardHeight = NAN;
    bool resizing = false;
    DispatcherTimer timer;
    GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    bool requested = false;
};

// Tempo como m:ss (ou h:mm:ss a partir de uma hora).
static std::wstring FormatTime(TimeSpan time) {
    long long seconds = std::max<long long>(0, std::chrono::duration_cast<std::chrono::seconds>(time).count());
    wchar_t text[32];

    if (seconds >= 3600) swprintf_s(text, L"%lld:%02lld:%02lld", seconds / 3600, seconds / 60 % 60, seconds % 60);
    else swprintf_s(text, L"%lld:%02lld", seconds / 60, seconds % 60);
    return text;
}

// Texto pequeno e apagado do tempo decorrido e da duração.
static Controls::TextBlock TimeText(HorizontalAlignment alignment) {
    Controls::TextBlock text;
    text.FontSize(11);
    text.Foreground(Solid(0x99));
    text.HorizontalAlignment(alignment);
    return text;
}

// Trilho cinza escuro com o preenchimento claro por cima (os dois com cantos redondos) e os tempos embaixo.
static void BuildTimeline(Timeline& timeline) {
    timeline.root.Name(L"ThemeMediaTimeline");
    timeline.root.Margin({0, 10, 0, 0});
    timeline.root.RowSpacing(4);
    timeline.root.Visibility(Visibility::Collapsed);
    for (int i = 0; i < 2; i++) timeline.root.RowDefinitions().Append(Controls::RowDefinition());

    timeline.track.Height(4);
    timeline.track.CornerRadius({2, 2, 2, 2});
    timeline.track.Background(Solid(0x30));
    timeline.fill.Height(4);
    timeline.fill.Width(0);
    timeline.fill.CornerRadius({2, 2, 2, 2});
    timeline.fill.Background(Solid(0xDD));
    timeline.fill.HorizontalAlignment(HorizontalAlignment::Left);
    timeline.root.Children().Append(timeline.track);
    timeline.root.Children().Append(timeline.fill);

    timeline.elapsed = TimeText(HorizontalAlignment::Left);
    timeline.duration = TimeText(HorizontalAlignment::Right);
    Controls::Grid::SetRow(timeline.elapsed, 1);
    Controls::Grid::SetRow(timeline.duration, 1);
    timeline.root.Children().Append(timeline.elapsed);
    timeline.root.Children().Append(timeline.duration);
}

// O cartão de mídia tem altura fixa (o Windows põe 178): com a barra à mostra ele cresce a altura dela, e volta ao
// valor do Windows quando ela some.
static void FitCard(Timeline& timeline) {
    if (!timeline.card) return;

    double extra = timeline.root.Visibility() == Visibility::Visible ? kTimelineHeight : 0;
    double target = timeline.cardHeight + extra;
    if (timeline.card.Height() == target || (std::isnan(target) && std::isnan(timeline.card.Height()))) return;

    timeline.resizing = true;
    timeline.card.Height(target);
    timeline.resizing = false;
}

// Mostra ou esconde a barra e ajusta a altura do cartão junto.
static void ShowTimeline(Timeline& timeline, bool visible) {
    auto visibility = visible ? Visibility::Visible : Visibility::Collapsed;
    if (timeline.root.Visibility() == visibility) return;

    timeline.root.Visibility(visibility);
    FitCard(timeline);
}

// Posição agora: a última informada pelo player, andando com o relógio enquanto toca, dentro do começo e do fim.
static TimeSpan CurrentPosition(GlobalSystemMediaTransportControlsSessionTimelineProperties const& properties, bool playing) {
    TimeSpan position = properties.Position();
    if (playing) position += winrt::clock::now() - properties.LastUpdatedTime();
    return std::clamp(position, properties.StartTime(), properties.EndTime());
}

// Um passo do timer: lê a linha do tempo da sessão de mídia atual (só leitura) e atualiza a barra; sem sessão ou sem
// duração, a barra some.
static void Refresh(Timeline& timeline) {
    auto session = timeline.manager ? timeline.manager.GetCurrentSession() : nullptr;
    auto properties = session ? session.GetTimelineProperties() : nullptr;
    TimeSpan length = properties ? properties.EndTime() - properties.StartTime() : TimeSpan{};
    if (length.count() <= 0) {
        ShowTimeline(timeline, false);
        return;
    }

    bool playing = session.GetPlaybackInfo().PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
    TimeSpan position = CurrentPosition(properties, playing) - properties.StartTime();
    double fraction = double(position.count()) / length.count();

    ShowTimeline(timeline, true);
    timeline.fill.Width(timeline.track.ActualWidth() * fraction);
    timeline.elapsed.Text(FormatTime(position));
    timeline.duration.Text(FormatTime(length));
}

// Pede o gerenciador de sessões uma vez, sem travar a thread da UI (a resposta chega depois).
static void RequestManager(std::shared_ptr<Timeline> const& timeline) {
    if (timeline->requested) return;

    timeline->requested = true;
    GlobalSystemMediaTransportControlsSessionManager::RequestAsync().Completed([timeline](auto const& operation, auto) {
        try { timeline->manager = operation.GetResults(); } catch (...) { Log(L"linha do tempo: sem acesso às sessões de mídia"); }
    });
}

// Liga o timer (~250 ms) só com o cartão de mídia à mostra: ao fechar as Configurações Rápidas o Windows colapsa o
// cartão, então com o painel fechado nada fica rodando.
static void SyncTimer(std::shared_ptr<Timeline> const& timeline) {
    bool visible = timeline->root.IsLoaded() && timeline->card && timeline->card.Visibility() == Visibility::Visible;
    if (visible == timeline->timer.IsEnabled()) return;

    if (!visible) {
        timeline->timer.Stop();
        return;
    }

    RequestManager(timeline);
    try { Refresh(*timeline); } catch (...) {}
    timeline->timer.Start();
}

// Acha o cartão de mídia (MediaTransportControlsRegion, o avô da barra, só ligado depois do Loaded) e acompanha a
// altura que o Windows dá a ele (a nossa troca é ignorada) e quando ele aparece ou some.
static void FollowCard(std::shared_ptr<Timeline> const& timeline) {
    auto card = VisualTreeHelper::GetParent(VisualTreeHelper::GetParent(timeline->root)).try_as<FrameworkElement>();
    if (!card || card.Name() != L"MediaTransportControlsRegion") return;

    timeline->card = card;
    timeline->cardHeight = card.Height();
    card.RegisterPropertyChangedCallback(FrameworkElement::HeightProperty(), [timeline](auto&&, auto&&) {
        try {
            if (timeline->resizing) return;
            timeline->cardHeight = timeline->card.Height();
            FitCard(*timeline);
        } catch (...) {}
    });
    card.RegisterPropertyChangedCallback(UIElement::VisibilityProperty(), [timeline](auto&&, auto&&) {
        try { SyncTimer(timeline); } catch (...) {}
    });
    FitCard(*timeline);
}

// Começa a acompanhar o cartão quando a barra entra na árvore; saindo dela, o timer para.
static void FollowPanel(std::shared_ptr<Timeline> const& timeline) {
    timeline->root.Loaded([timeline](auto&&, auto&&) {
        try {
            if (!timeline->card) FollowCard(timeline);
            SyncTimer(timeline);
        } catch (...) {}
    });
    timeline->root.Unloaded([timeline](auto&&, auto&&) {
        try { SyncTimer(timeline); } catch (...) {}
    });
}

// Cria a barra de tempo da música (só leitura) para o chamador pôr no cartão de mídia.
FrameworkElement CreateMediaTimeline() {
    auto timeline = std::make_shared<Timeline>();
    BuildTimeline(*timeline);

    timeline->timer.Interval(std::chrono::milliseconds(250));
    timeline->timer.Tick([timeline](auto&&, auto&&) {
        try {
            Refresh(*timeline);
        } catch (...) {
            ShowTimeline(*timeline, false);
        }
    });
    FollowPanel(timeline);
    return timeline->root;
}