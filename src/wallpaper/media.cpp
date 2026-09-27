#include "wallpaper/media.h"
#include "wallpaper/desktop.h"

#include <shcore.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <mutex>
#include <thread>

using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Media::Control;

struct CoverState {
    std::wstring song;
    std::shared_ptr<std::vector<BYTE>> pixels;
    int attempts = 0;
};

static std::mutex g_mutex;
static NowPlaying g_nowPlaying;

// Lado da capa na tela, em pixels (90 DIPs).
int CoverSize() {
    return int(90 * GetDpiForSystem() / 96.f);
}

// Capa do álbum (o Spotify manda um JPEG pequeno) já no tamanho da tela, em BGRA pré-multiplicado. Estica para
// quadrado: capa de álbum já é quadrada.
static std::shared_ptr<std::vector<BYTE>> DecodeCover(winrt::Windows::Storage::Streams::IRandomAccessStreamReference const& reference) {
    if (!reference) return nullptr;

    auto stream = reference.OpenReadAsync().get();
    ComPtr<IStream> input;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    int size = CoverSize();

    if (FAILED(CreateStreamOverRandomAccessStream(winrt::get_unknown(stream), IID_PPV_ARGS(&input))) ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(wic->CreateDecoderFromStream(input.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) || FAILED(decoder->GetFrame(0, &frame)))
        return nullptr;

    wic->CreateBitmapScaler(&scaler);
    scaler->Initialize(frame.Get(), size, size, WICBitmapInterpolationModeHighQualityCubic);
    wic->CreateFormatConverter(&converter);
    converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);

    auto pixels = std::make_shared<std::vector<BYTE>>((size_t)size * size * 4);
    return SUCCEEDED(converter->CopyPixels(nullptr, size * 4, (UINT)pixels->size(), pixels->data())) ? pixels : nullptr;
}

// Capa da música atual: espera 1 s depois da troca (a capa às vezes chega depois do título) e tenta por alguns segundos.
static void UpdateCover(GlobalSystemMediaTransportControlsSessionMediaProperties const& properties, NowPlaying const& song, CoverState& cover) {
    std::wstring key = song.title + L"\n" + song.artist;
    if (key != cover.song) {
        cover.song = key;
        cover.pixels = nullptr;
        cover.attempts = 0;
    }

    if (!cover.pixels && ++cover.attempts >= 2 && cover.attempts < 10) {
        try { cover.pixels = DecodeCover(properties.Thumbnail()); } catch (...) {}
    }
}

// O que o Spotify está tocando agora (só leitura: não controla nada); vazio se ele não tiver sessão de mídia.
static NowPlaying ReadSpotify(GlobalSystemMediaTransportControlsSessionManager const& manager, CoverState& cover) {
    NowPlaying song;

    for (auto session : manager.GetSessions()) {
        if (std::wstring_view(session.SourceAppUserModelId()).find(L"Spotify") == std::wstring_view::npos) continue;

        song.playing = session.GetPlaybackInfo().PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        auto properties = session.TryGetMediaPropertiesAsync().get();
        song.title = properties.Title();
        song.artist = properties.Artist();
        UpdateCover(properties, song, cover);
        song.cover = cover.pixels;
        break;
    }
    return song;
}

// Thread da mídia: lê o Spotify 1x por segundo (GlobalSystemMediaTransportControls); com o fundo oculto (jogo aberto)
// nem pergunta. Se o gerenciador de sessões falhar, pede outro na próxima volta.
static void MediaLoop() {
    winrt::init_apartment();
    GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    CoverState cover;

    for (;;) {
        NowPlaying song;
        try {
            if (!manager) manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            song = ReadSpotify(manager, cover);
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

// Última leitura do Spotify.
NowPlaying CurrentMedia() {
    std::lock_guard lock(g_mutex);
    return g_nowPlaying;
}
