#pragma once

#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

ComPtr<IDWriteTextFormat> SingleLineFont(IDWriteFactory* dwrite, const wchar_t* family, float pixels,
                                         DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL);
float TextWidth(IDWriteFactory* dwrite, std::wstring const& text, IDWriteTextFormat* font);
