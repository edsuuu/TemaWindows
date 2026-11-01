#include "wallpaper/video_theme.h"

#include <d3d11_4.h>
#include <mfapi.h>

// O motor de mídia exige quem ouça os eventos dele; o vídeo não precisa de nenhum (erro = fica no último quadro).
struct IgnoreEvents : IMFMediaEngineNotify {
    LONG references = 1;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (id != __uuidof(IUnknown) && id != __uuidof(IMFMediaEngineNotify)) {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        *out = this;
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG left = InterlockedDecrement(&references);
        if (!left) delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE EventNotify(DWORD, DWORD_PTR, DWORD) override { return S_OK; }
};

// Abre o vídeo do papel de parede (FundoVideo: mp4, mov, wmv, mkv...) no Media Foundation, decodificado pela placa de
// vídeo no mesmo dispositivo do Direct3D (que passa a aceitar outras threads), sem som e em loop.
bool VideoTheme::Open(ID3D11Device* d3d, std::wstring const& path) {
    Close();
    static bool started = SUCCEEDED(MFStartup(MF_VERSION));
    if (!started || path.empty()) return false;

    ComPtr<ID3D11Multithread> multithread;
    if (SUCCEEDED(d3d->QueryInterface(IID_PPV_ARGS(&multithread)))) multithread->SetMultithreadProtected(TRUE);

    UINT token;
    ComPtr<IMFMediaEngineClassFactory> factory;
    ComPtr<IMFAttributes> attributes;
    ComPtr<IMFMediaEngineNotify> notify;
    notify.Attach(new IgnoreEvents);
    if (FAILED(MFCreateDXGIDeviceManager(&token, &manager)) || FAILED(manager->ResetDevice(d3d, token)) ||
        FAILED(CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(MFCreateAttributes(&attributes, 3)))
        return false;

    attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, manager.Get());
    attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify.Get());
    attributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);
    if (FAILED(factory->CreateInstance(0, attributes.Get(), &engine))) return false;

    BSTR source = SysAllocString(path.c_str());
    engine->SetMuted(TRUE);
    engine->SetLoop(TRUE);
    engine->SetSource(source);
    SysFreeString(source);
    device = d3d;
    if (!paused) engine->Play();
    return true;
}

// Para o vídeo e solta tudo.
void VideoTheme::Close() {
    if (engine) engine->Shutdown();
    engine = nullptr;
    manager = nullptr;
    texture = nullptr;
    view = nullptr;
    device = nullptr;
    width = height = 0;
    ready = false;
}

// Pausa com o fundo escondido (jogo, tela cheia, bloqueio) e volta de onde parou.
void VideoTheme::Pause(bool pause) {
    if (pause == paused) return;
    paused = pause;
    if (!engine) return;
    if (pause) engine->Pause();
    else engine->Play();
}

// Copia o quadro novo (se houver) para a textura do tamanho do vídeo e a devolve; nada antes do primeiro quadro.
ID3D11ShaderResourceView* VideoTheme::Frame() {
    if (!engine) return nullptr;
    if (!texture) {
        if (!engine->HasVideo() || FAILED(engine->GetNativeVideoSize(&width, &height)) || !width || !height) return nullptr;
        D3D11_TEXTURE2D_DESC desc{width, height, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                                  D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET};
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture)) || FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &view))) {
            texture = nullptr;
            return nullptr;
        }
    }

    LONGLONG time;
    if (engine->OnVideoStreamTick(&time) == S_OK) {
        MFVideoNormalizedRect source{0, 0, 1, 1};
        RECT target{0, 0, LONG(width), LONG(height)};
        MFARGB border{0, 0, 0, 255};
        ready = SUCCEEDED(engine->TransferVideoFrame(texture.Get(), &source, &target, &border)) || ready;
    }
    return ready ? view.Get() : nullptr;
}

// Largura / altura do vídeo.
float VideoTheme::Aspect() const {
    return height ? float(width) / height : 1;
}
