#include "wallpaper/media.h"
#include "wallpaper/audio.h"
#include "wallpaper/desktop.h"
#include "wallpaper/timing.h"

#include <shcore.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <algorithm>
#include <mutex>
#include <thread>

using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Media::Control;

struct CoverState {
    std::wstring song;
    std::shared_ptr<std::vector<BYTE>> pixels;
    int attempts = 0;
};

struct TimelineAnchor {
    winrt::Windows::Foundation::TimeSpan position{};
    bool playing = false;
    winrt::clock::time_point seen{};
};

struct SpotifyState {
    CoverState cover;
    TimelineAnchor anchor;
    int quietReads = 0;
};

static std::mutex g_mutex;
static NowPlaying g_nowPlaying;

// Lado da capa na tela, em pixels (90 DIPs).
int CoverSize() {
    return int(90 * GetDpiForSystem() / 96.f);
}

// Imagem (JPEG/PNG) num quadrado de `size` px, em BGRA pré-multiplicado. Estica para quadrado: capa de álbum já é
// quadrada.
std::shared_ptr<std::vector<BYTE>> DecodeImage(IStream* input, int size) {
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;

    if (!input || FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(wic->CreateDecoderFromStream(input, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) || FAILED(decoder->GetFrame(0, &frame)))
        return nullptr;

    wic->CreateBitmapScaler(&scaler);
    scaler->Initialize(frame.Get(), size, size, WICBitmapInterpolationModeHighQualityCubic);
    wic->CreateFormatConverter(&converter);
    converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);

    auto pixels = std::make_shared<std::vector<BYTE>>((size_t)size * size * 4);
    return SUCCEEDED(converter->CopyPixels(nullptr, size * 4, (UINT)pixels->size(), pixels->data())) ? pixels : nullptr;
}

// Capa do álbum que o Spotify entrega na sessão de mídia (um JPEG pequeno), no tamanho da tela.
static std::shared_ptr<std::vector<BYTE>> DecodeCover(winrt::Windows::Storage::Streams::IRandomAccessStreamReference const& reference) {
    if (!reference) return nullptr;

    auto stream = reference.OpenReadAsync().get();
    ComPtr<IStream> input;
    CreateStreamOverRandomAccessStream(winrt::get_unknown(stream), IID_PPV_ARGS(&input));
    return DecodeImage(input.Get(), CoverSize());
}

// Capa da música atual. Logo depois da troca o Spotify às vezes ainda entrega a capa da música anterior, ou uma pela
// metade: tenta a cada segundo até ter uma (a partir de 1 s depois da troca), relê aos 3 s e aos 6 s e depois a cada
// 10 s, e só troca quando a imagem mudou (a mesma imagem não refaz o fade no cartão).
static void UpdateCover(GlobalSystemMediaTransportControlsSessionMediaProperties const& properties, NowPlaying const& song, CoverState& cover) {
    std::wstring key = song.title + L"\n" + song.artist;
    if (key != cover.song) {
        cover.song = key;
        cover.pixels = nullptr;
        cover.attempts = 0;
    }

    int attempt = ++cover.attempts;
    bool due = (!cover.pixels && attempt >= 2 && attempt < 10) || attempt == 4 || attempt == 7 || attempt % 10 == 0;
    if (!due) return;

    std::shared_ptr<std::vector<BYTE>> pixels;
    try { pixels = DecodeCover(properties.Thumbnail()); } catch (...) {}
    if (pixels && (!cover.pixels || *pixels != *cover.pixels)) cover.pixels = pixels;
}

// Posição e duração da música em segundos, pela linha do tempo da sessão: a última posição informada pelo player,
// andando com o relógio enquanto toca, dentro do começo e do fim. O Spotify às vezes deixa a hora da posição velha
// (ao voltar do pause, ela ainda é a de antes da pausa), e somar isso adiantava o tempo: o quanto andou fica limitado
// ao tempo desde que a posição ou o play/pausa mudou aqui (+1,1 s, o intervalo das leituras). Marca a hora da leitura
// para o quadro continuar andando entre uma leitura e outra.
static void ReadTimeline(GlobalSystemMediaTransportControlsSession const& session, NowPlaying& song, TimelineAnchor& anchor) {
    auto properties = session.GetTimelineProperties();
    auto length = properties.EndTime() - properties.StartTime();
    if (length.count() <= 0) return;

    auto position = properties.Position();
    auto now = winrt::clock::now();
    if (position != anchor.position || song.playing != anchor.playing) anchor = {position, song.playing, now};
    if (song.playing) position += std::min(now - properties.LastUpdatedTime(), now - anchor.seen + std::chrono::milliseconds(1100));
    position = std::clamp(position, properties.StartTime(), properties.EndTime()) - properties.StartTime();

    song.position = std::chrono::duration<double>(position).count();
    song.duration = std::chrono::duration<double>(length).count();
    song.readAt = Now();
}

// O que o Spotify está tocando agora (tocando ou pausado); vazio se ele não tiver sessão de mídia. Tocando, diz também
// se o som sai neste PC: sem a sessão de áudio dele por 3 leituras seguidas (entre uma música e outra ela some por um
// instante), está tocando em outro aparelho.
static NowPlaying ReadSpotify(GlobalSystemMediaTransportControlsSessionManager const& manager, SpotifyState& state) {
    NowPlaying song;

    for (auto session : manager.GetSessions()) {
        if (std::wstring_view(session.SourceAppUserModelId()).find(L"Spotify") == std::wstring_view::npos) continue;

        song.playing = session.GetPlaybackInfo().PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        auto properties = session.TryGetMediaPropertiesAsync().get();
        song.title = properties.Title();
        song.artist = properties.Artist();
        UpdateCover(properties, song, state.cover);
        song.cover = state.cover.pixels;
        ReadTimeline(session, song, state.anchor);
        state.quietReads = song.playing && !SpotifyAudible() ? state.quietReads + 1 : 0;
        song.elsewhere = state.quietReads >= 3;
        break;
    }
    return song;
}

// Thread da mídia: lê o Spotify 1x por segundo (GlobalSystemMediaTransportControls); com o fundo oculto (jogo aberto)
// nem pergunta. Se o gerenciador de sessões falhar, pede outro na próxima volta.
static void MediaLoop() {
    winrt::init_apartment();
    GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    SpotifyState state;

    for (;;) {
        NowPlaying song;
        try {
            if (!manager) manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            song = ReadSpotify(manager, state);
        } catch (...) {
            manager = nullptr;
        }

        {
            std::lock_guard lock(g_mutex);
            g_nowPlaying = song;
        }

        do Sleep(1000);
        while (WallpaperHidden());
    }
}

// Começa a acompanhar o Spotify numa thread à parte.
void StartMediaWatcher() {
    std::thread(MediaLoop).detach();
}

// Comando para a sessão do Spotify pelos controles de mídia do Windows (0 voltar, 1 tocar/pausar, 2 avançar), numa
// thread à parte. Tocar/pausar troca o ícone na hora, sem esperar a próxima leitura.
void SendMediaCommand(int button) {
    if (button == 1) {
        std::lock_guard lock(g_mutex);
        g_nowPlaying.playing = !g_nowPlaying.playing;
    }

    std::thread([button] {
        try {
            winrt::init_apartment();
            auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            for (auto session : manager.GetSessions()) {
                if (std::wstring_view(session.SourceAppUserModelId()).find(L"Spotify") == std::wstring_view::npos) continue;

                if (button == 0) session.TrySkipPreviousAsync().get();
                if (button == 1) session.TryTogglePlayPauseAsync().get();
                if (button == 2) session.TrySkipNextAsync().get();
                break;
            }
        } catch (...) {}
    }).detach();
}

// Última leitura do Spotify.
NowPlaying CurrentMedia() {
    std::lock_guard lock(g_mutex);
    return g_nowPlaying;
}
