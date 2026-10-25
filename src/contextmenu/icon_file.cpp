#include "contextmenu/contextmenu.h"
#include "common/paths.h"

#include <cstdint>
#include <cstdio>

// Pixels 32 bits (BGRA, de baixo para cima) do bitmap de um item de menu, com alfa direto: bitmap sem alfa fica opaco
// e o pré-multiplicado (o que o Explorer usa) é desfeito.
static bool MenuBitmapPixels(HBITMAP bitmap, int& width, int& height, std::vector<uint32_t>& pixels) {
    BITMAP info{};
    if (!GetObjectW(bitmap, sizeof info, &info) || info.bmWidth <= 0 || info.bmWidth > 256 || info.bmHeight <= 0 || info.bmHeight > 256) return false;

    width = info.bmWidth;
    height = info.bmHeight;
    pixels.resize(size_t(width) * height);
    BITMAPINFOHEADER header{sizeof header, width, height, 1, 32, BI_RGB};
    HDC screen = GetDC(nullptr);
    int rows = GetDIBits(screen, bitmap, 0, height, pixels.data(), reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen);
    if (rows != height) return false;

    bool alpha = false, premultiplied = true;
    for (uint32_t pixel : pixels) {
        uint32_t a = pixel >> 24;
        alpha |= a != 0;
        premultiplied &= (pixel & 0xFF) <= a && (pixel >> 8 & 0xFF) <= a && (pixel >> 16 & 0xFF) <= a;
    }
    for (uint32_t& pixel : pixels) {
        uint32_t a = alpha ? pixel >> 24 : 255;
        if (alpha && premultiplied && a && a < 255) {
            uint32_t b = (pixel & 0xFF) * 255 / a, g = (pixel >> 8 & 0xFF) * 255 / a, r = (pixel >> 16 & 0xFF) * 255 / a;
            pixel = b | g << 8 | r << 16;
        }
        pixel = (pixel & 0xFFFFFF) | a << 24;
    }
    return true;
}

// Salva o bitmap de um item de menu como .ico em cache\contexto\ (nome pelo conteúdo), porque o menu novo só aceita
// ícone por caminho de arquivo. Devolve o caminho, ou vazio se o bitmap não der.
std::wstring SaveMenuIcon(HBITMAP bitmap) {
    int width, height;
    std::vector<uint32_t> pixels;
    if (!MenuBitmapPixels(bitmap, width, height, pixels)) return L"";

    uint32_t hash = 2166136261u;
    for (uint32_t pixel : pixels) hash = (hash ^ pixel) * 16777619u;
    wchar_t name[32];
    swprintf_s(name, L"%08x.ico", hash);
    std::wstring folder = ProjectPath(L"cache\\contexto");
    std::wstring path = folder + L"\\" + name;
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return path;

    CreateDirectoryW(folder.c_str(), nullptr);
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") || !file) return L"";

    uint32_t maskBytes = (width + 31) / 32 * 4 * height, pixelBytes = uint32_t(pixels.size() * 4);
    BITMAPINFOHEADER header{sizeof header, width, height * 2, 1, 32, BI_RGB, pixelBytes + maskBytes};
    uint16_t directory[3] = {0, 1, 1};
    uint8_t entry[16] = {uint8_t(width % 256), uint8_t(height % 256), 0, 0, 1, 0, 32, 0};
    uint32_t sizes[2] = {uint32_t(sizeof header) + pixelBytes + maskBytes, 6 + 16};
    memcpy(entry + 8, sizes, 8);
    std::vector<uint8_t> mask(maskBytes);

    fwrite(directory, sizeof directory, 1, file);
    fwrite(entry, sizeof entry, 1, file);
    fwrite(&header, sizeof header, 1, file);
    fwrite(pixels.data(), pixelBytes, 1, file);
    fwrite(mask.data(), maskBytes, 1, file);
    fclose(file);
    return path;
}
