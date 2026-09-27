#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>

using Microsoft::WRL::ComPtr;

// Janela do FundoVivo: filha de um WorkerW, que é filho do Progman.
static HWND FindWallpaperWindow() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    HWND wallpaper = nullptr;

    for (HWND worker = nullptr; !wallpaper && (worker = FindWindowExW(progman, worker, L"WorkerW", nullptr));)
        wallpaper = FindWindowExW(worker, nullptr, L"FundoVivo", nullptr);
    return wallpaper;
}

// Grava BGRA de cima para baixo num PNG, com o alfa opaco.
static bool SavePng(const wchar_t* path, BYTE* pixels, UINT width, UINT height) {
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;

    for (UINT i = 0; i < width * height; i++) pixels[i * 4 + 3] = 255;

    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    wic->CreateStream(&stream);
    if (FAILED(stream->InitializeFromFilename(path, GENERIC_WRITE))) return false;

    wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    encoder->CreateNewFrame(&frame, nullptr);
    frame->Initialize(nullptr);
    frame->SetSize(width, height);
    frame->SetPixelFormat(&format);
    frame->WritePixels(height, width * 4, width * 4 * height, pixels);
    frame->Commit();
    return SUCCEEDED(encoder->Commit());
}

// capture-wallpaper.exe <saida.png>: print do FundoVivo inteiro (todos os monitores, mesmo atrás de janelas), direto da
// janela dele via PrintWindow(PW_RENDERFULLCONTENT); serve para conferir a emenda entre monitores e o canto dos widgets.
int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        fwprintf(stderr, L"uso: capture-wallpaper <saida.png>\n");
        return 1;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    HWND wallpaper = FindWallpaperWindow();
    if (!wallpaper) {
        fwprintf(stderr, L"FundoVivo não está rodando\n");
        return 1;
    }

    RECT client;
    GetClientRect(wallpaper, &client);
    HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    BITMAPINFO info{{sizeof(BITMAPINFOHEADER), client.right, -client.bottom, 1, 32, BI_RGB}};
    void* bits;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(memory, bitmap);

    if (!PrintWindow(wallpaper, memory, PW_CLIENTONLY | PW_RENDERFULLCONTENT)) {
        fwprintf(stderr, L"PrintWindow falhou\n");
        return 1;
    }
    if (!SavePng(argv[1], (BYTE*)bits, client.right, client.bottom)) return 1;

    wprintf(L"%ldx%ld -> %s\n", client.right, client.bottom, argv[1]);
    return 0;
}
