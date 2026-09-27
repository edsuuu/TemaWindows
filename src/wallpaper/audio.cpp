#include "wallpaper/audio.h"

#include <mmdeviceapi.h>
#include <algorithm>
#include <cmath>
#include <complex>

// Loopback da saída padrão (onde o Spotify toca): mono, float, 48 kHz (o Windows converte). A captura abre uma sessão
// de áudio em nome do FundoVivo e DISPLAY_HIDE a tira do Mixer de Volume. Pega todo o som dessa saída, não só o do
// Spotify: o loopback só do processo dele abria uma sessão no Mixer que não dá para esconder
// (AUDCLNT_E_INVALID_STREAM_FLAG).
ComPtr<IAudioCaptureClient> StartLoopbackCapture(ComPtr<IAudioClient>& client) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioCaptureClient> capture;
    WAVEFORMATEX format{WAVE_FORMAT_IEEE_FLOAT, 1, 48000, 48000 * 4, 4, 32, 0};
    DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY |
                  AUDCLNT_SESSIONFLAGS_DISPLAY_HIDE | AUDCLNT_SESSIONFLAGS_EXPIREWHENUNOWNED;

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device)) ||
        FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)client.ReleaseAndGetAddressOf())) ||
        FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 2000000, 0, &format, nullptr)) ||
        FAILED(client->GetService(IID_PPV_ARGS(&capture))) || FAILED(client->Start()))
        return nullptr;
    return capture;
}

// Guarda as amostras novas no anel das últimas 2048 (silêncio vira zero).
void Equalizer::Read(IAudioCaptureClient* capture) {
    BYTE* data;
    UINT32 frames;
    DWORD flags;

    while (capture && capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr) == S_OK) {
        for (UINT i = 0; i < frames; i++, position = (position + 1) % kSamples)
            ring[position] = (flags & AUDCLNT_BUFFERFLAGS_SILENT) ? 0 : ((const float*)data)[i];
        capture->ReleaseBuffer(frames);
    }
}

// FFT radix-2 no lugar.
static void Fft(std::complex<float>* x, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }

    for (int length = 2; length <= n; length <<= 1) {
        auto step = std::polar(1.f, -6.2831853f / length);
        for (int i = 0; i < n; i += length) {
            std::complex<float> w = 1;
            for (int k = 0; k < length / 2; k++, w *= step) {
                auto u = x[i + k], v = x[i + k + length / 2] * w;
                x[i + k] = u + v;
                x[i + k + length / 2] = u - v;
            }
        }
    }
}

// Energia (dB) de cada banda, de 40 Hz a 16 kHz em escala log. Embaixo a FFT tem menos faixas que as bandas: cada
// banda pega pelo menos uma faixa só dela.
static void BandLevels(std::complex<float> const* spectrum, float* decibels) {
    for (int band = 0, high = 1; band < Equalizer::kBands; band++) {
        int low = high;
        high = std::max(low + 1, int(40 * powf(400.f, float(band + 1) / Equalizer::kBands) * Equalizer::kSamples / 48000));

        float energy = 1e-12f;
        for (int k = low; k < high; k++) energy += std::norm(spectrum[k]);
        decibels[band] = 10 * log10f(energy);
    }
}

// Equalizador: FFT das últimas 2048 amostras (janela de Hann, ~23 Hz por faixa) somada em 28 bandas. Cada banda em dB
// relativo à própria média do último ~1,5 s: na média a barra fica baixinha, 14 dB acima enche (volume alto ou baixo
// dá o mesmo efeito, e graves e agudos se mexem igual). Silêncio (ou banda 55 dB abaixo da mais forte) fica zerado e
// não mexe na média, para não "levantar" ruído nem estourar as barras quando a próxima música começa. Sobe rápido e
// cai devagar.
void Equalizer::Update(bool active, float dt) {
    float target[kBands] = {};

    if (active) {
        std::complex<float> spectrum[kSamples];
        float decibels[kBands];
        for (int i = 0; i < kSamples; i++) spectrum[i] = ring[(position + i) % kSamples] * (0.5f - 0.5f * cosf(6.2831853f * i / kSamples));
        Fft(spectrum, kSamples);
        BandLevels(spectrum, decibels);

        float noiseFloor = std::max(-55.f, *std::max_element(average, average + kBands) - 55);
        for (int band = 0; band < kBands; band++) {
            if (decibels[band] < noiseFloor) continue;

            average[band] += (decibels[band] - average[band]) * std::min(1.f, dt / 1.5f);
            target[band] = std::clamp((decibels[band] - average[band] + 4) / 18, 0.f, 1.f);
        }
    }

    for (int band = 0; band < kBands; band++)
        level[band] += (target[band] - level[band]) * std::min(1.f, dt * (target[band] > level[band] ? 30 : 5));
}
