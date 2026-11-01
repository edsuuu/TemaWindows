#include "wallpaper/widget_options.h"
#include "common/registry.h"

#include <algorithm>

// Opções do bloco do canto (o FundoConfig grava, o FundoVivo relê a cada meio segundo). Liga/desliga: DWORD, ligado
// quando não existe. Posicao 0 a 8 e PosicaoClima 0 a 2; Escala (50 a 200%), Margem (DIPs da borda, até 400) e Sombra
// (0 a 100% de escurecimento atrás do bloco); MonitoresOcultos: nomes GDI ("\\.\DISPLAY2") entre barras, "|a|b|".
WidgetOptions ReadWidgetOptions() {
    WidgetOptions o;
    for (size_t i = 0; i < std::size(kPartSwitches); i++) o.parts[i] = Enabled(kPartSwitches[i].name);
    for (int i = 0; i < FieldCount; i++) o.fields[i] = Enabled(kFieldSwitches[i].name);
    o.json = Enabled(L"Json");
    o.fahrenheit = Setting(L"ClimaUnidade", 0) == 1;
    o.range = Enabled(L"ClimaMaxMin");
    o.pause = Enabled(L"FundoPausa");
    o.position = int(Setting(L"Posicao", 2) % 9);
    o.weatherSide = int(Setting(L"PosicaoClima", 2) % 3);
    o.size = int(std::clamp<DWORD>(Setting(L"Escala", 100), 50, 200));
    o.margin = int(std::min<DWORD>(Setting(L"Margem", 44), 400));
    o.shadow = int(std::min<DWORD>(Setting(L"Sombra", 75), 100));
    o.hiddenMonitors = SettingText(L"MonitoresOcultos", L"");
    return o;
}
