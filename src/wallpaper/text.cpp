#include "wallpaper/text.h"

// Formato de texto de uma linha só, na fonte, no tamanho (já em pixels) e no peso dados, em pt-BR.
ComPtr<IDWriteTextFormat> SingleLineFont(IDWriteFactory* dwrite, const wchar_t* family, float pixels, DWRITE_FONT_WEIGHT weight) {
    ComPtr<IDWriteTextFormat> font;
    dwrite->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, pixels, L"pt-br", &font);
    font->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return font;
}

// Largura do texto numa linha, em pixels.
float TextWidth(IDWriteFactory* dwrite, std::wstring const& text, IDWriteTextFormat* font) {
    ComPtr<IDWriteTextLayout> layout;
    DWRITE_TEXT_METRICS metrics{};
    dwrite->CreateTextLayout(text.c_str(), (UINT)text.size(), font, 4096, 4096, &layout);
    layout->GetMetrics(&metrics);
    return metrics.width;
}
