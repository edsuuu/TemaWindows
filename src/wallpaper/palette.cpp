#include "wallpaper/palette.h"
#include "common/registry.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

static D2D1_COLOR_F chosen[size_t(Ink::Count)];
static int version = -1;

// Cor escolhida para `ink` (REG_SZ "#RRGGBB" em HKCU\Software\TemaBarra, que o FundoConfig.exe edita); sem ela, ou
// fora desse formato, o padrão.
D2D1_COLOR_F ReadInk(Ink ink) {
    D2D1_COLOR_F standard = kInks[size_t(ink)].standard;
    std::wstring text = SettingText(kInks[size_t(ink)].name, L"");
    if (text.size() != 7 || text[0] != L'#') return standard;

    wchar_t* end;
    unsigned long rgb = wcstoul(text.c_str() + 1, &end, 16);
    if (*end) return standard;
    return {(rgb >> 16 & 255) / 255.f, (rgb >> 8 & 255) / 255.f, (rgb & 255) / 255.f, 1};
}

// Troca as cores em uso (na ordem de kInks); a versão só muda quando alguma cor mudou, para o painel do JSON refazer o
// bitmap. A prévia do FundoConfig usa direto, com as cores ainda não salvas.
void SetPalette(D2D1_COLOR_F const* colors) {
    if (version >= 0 && !memcmp(colors, chosen, sizeof chosen)) return;
    memcpy(chosen, colors, sizeof chosen);
    version++;
}

// Relê as cores dos widgets do registro (chamado a cada meio segundo).
void ReloadPalette() {
    D2D1_COLOR_F next[size_t(Ink::Count)];
    for (size_t i = 0; i < std::size(kInks); i++) next[i] = ReadInk(Ink(i));
    SetPalette(next);
}

// Muda a cada troca de cor.
int PaletteVersion() {
    return version;
}

// Cor escolhida para `ink`, com o brilho ajustado como o cinza `gray` estava para o padrão dela (com as cores padrão,
// sai exatamente o cinza de antes; com uma cor escolhida, as variações mais claras e mais escuras continuam).
D2D1_COLOR_F Paint(Ink ink, float gray, float alpha) {
    D2D1_COLOR_F const& standard = kInks[size_t(ink)].standard;
    D2D1_COLOR_F const& color = chosen[size_t(ink)];
    float factor = gray / std::max({standard.r, standard.g, standard.b});
    return {std::min(color.r * factor, 1.f), std::min(color.g * factor, 1.f), std::min(color.b * factor, 1.f), alpha};
}
