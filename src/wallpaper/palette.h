#pragma once

#include <d2d1.h>
#include <iterator>

enum class Ink { Text, Soft, Accent, Equalizer, Peak, JsonKey, JsonValue, JsonPunctuation, Count };

struct InkSetting {
    const wchar_t* name;
    const wchar_t* label;
    D2D1_COLOR_F standard;
};

inline constexpr InkSetting kInks[] = {
    {L"CorTexto", L"Texto (música, % dos anéis, clima)", {0.95f, 0.95f, 0.95f, 1}},
    {L"CorTextoSuave", L"Texto suave (artista, legendas, céu)", {0.78f, 0.78f, 0.78f, 1}},
    {L"CorAnel", L"Anéis e barra de progresso", {0.95f, 0.95f, 0.95f, 1}},
    {L"CorEqualizador", L"Equalizador", {0.9f, 0.9f, 0.9f, 1}},
    {L"CorPico", L"Pico do equalizador", {0.95f, 0.55f, 0.55f, 1}},
    {L"CorJsonChave", L"JSON: chaves", {0.66f, 0.66f, 0.66f, 1}},
    {L"CorJsonValor", L"JSON: valores", {0.9f, 0.9f, 0.9f, 1}},
    {L"CorJsonPontuacao", L"JSON: pontuação", {0.46f, 0.46f, 0.46f, 1}},
};

static_assert(std::size(kInks) == size_t(Ink::Count));

D2D1_COLOR_F ReadInk(Ink ink);
void SetPalette(D2D1_COLOR_F const* colors);
void ReloadPalette();
int PaletteVersion();
D2D1_COLOR_F Paint(Ink ink, float gray, float alpha);
