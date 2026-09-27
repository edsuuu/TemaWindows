#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>

using Microsoft::WRL::ComPtr;

// Duplicação do monitor pedindo BGRA de 8 bits: com HDR/cor avançada o padrão seria FP16, e assim o Windows converte.
static ComPtr<IDXGIOutputDuplication> DuplicateMonitor(ID3D11Device* device, int monitor) {
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIOutput> output;
    ComPtr<IDXGIOutput5> output5;
    ComPtr<IDXGIOutputDuplication> duplication;
    DXGI_FORMAT formats[] = {DXGI_FORMAT_B8G8R8A8_UNORM};

    device->QueryInterface(IID_PPV_ARGS(&dxgi));
    dxgi->GetAdapter(&adapter);
    if (FAILED(adapter->EnumOutputs(monitor, &output))) return nullptr;

    output.As(&output5);
    if (FAILED(output5->DuplicateOutput1(device, 0, 1, formats, &duplication))) return nullptr;
    return duplication;
}

// Primeiro quadro com imagem de verdade (o primeiro às vezes vem vazio): tenta alguns. Com a tela bloqueada não há quadro.
static ComPtr<ID3D11Texture2D> AcquireFrame(IDXGIOutputDuplication* duplication) {
    ComPtr<ID3D11Texture2D> frame;

    for (int i = 0; i < 20 && !frame; i++) {
        DXGI_OUTDUPL_FRAME_INFO info;
        ComPtr<IDXGIResource> resource;
        if (FAILED(duplication->AcquireNextFrame(200, &info, &resource))) continue;

        if (info.LastPresentTime.QuadPart) resource.As(&frame);
        else duplication->ReleaseFrame();
    }
    return frame;
}

// Grava BGRA num PNG. PNG não tem 32bppBGR (o codificador trocaria para 24 bits), então o alfa vira opaco.
static bool SavePng(const wchar_t* path, BYTE* pixels, UINT width, UINT height, UINT pitch) {
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;

    for (UINT y = 0; y < height; y++)
        for (UINT x = 0; x < width; x++) pixels[y * pitch + x * 4 + 3] = 255;

    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    wic->CreateStream(&stream);
    if (FAILED(stream->InitializeFromFilename(path, GENERIC_WRITE))) return false;

    wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    encoder->CreateNewFrame(&frame, nullptr);
    frame->Initialize(nullptr);
    frame->SetSize(width, height);
    frame->SetPixelFormat(&format);
    frame->WritePixels(height, pitch, pitch * height, pixels);
    frame->Commit();
    return SUCCEEDED(encoder->Commit());
}

// capture.exe <saida.png> [monitor]: print da tela como ela aparece de verdade (DXGI Desktop Duplication), com blur,
// acrílico e tudo que o DWM compõe; CopyFromScreen/BitBlt não pegam esses efeitos.
int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        fwprintf(stderr, L"uso: capture <saida.png> [monitor]\n");
        return 1;
    }

    int monitor = argc > 2 ? _wtoi(argv[2]) : 0;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);

    auto duplication = DuplicateMonitor(device.Get(), monitor);
    if (!duplication) {
        fwprintf(stderr, L"monitor %d não existe ou não duplica\n", monitor);
        return 1;
    }

    auto frame = AcquireFrame(duplication.Get());
    if (!frame) {
        fwprintf(stderr, L"sem quadro (tela bloqueada?)\n");
        return 1;
    }

    D3D11_TEXTURE2D_DESC desc;
    frame->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    device->CreateTexture2D(&desc, nullptr, &staging);
    context->CopyResource(staging.Get(), frame.Get());
    duplication->ReleaseFrame();

    D3D11_MAPPED_SUBRESOURCE mapped;
    context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    bool saved = SavePng(argv[1], (BYTE*)mapped.pData, desc.Width, desc.Height, mapped.RowPitch);
    context->Unmap(staging.Get(), 0);
    if (!saved) return 1;

    wprintf(L"%ux%u -> %s\n", desc.Width, desc.Height, argv[1]);
    return 0;
}
