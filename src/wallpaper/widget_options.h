#pragma once

#include <iterator>
#include <string>

// Campos do JSON que dá para esconder, na ordem em que aparecem.
enum JsonField { FieldUptime, FieldDate, FieldLight, FieldCpu, FieldGpu, FieldRam, FieldMonitors, FieldCount };

struct Switch {
    const wchar_t* name;
    const wchar_t* label;
};

inline constexpr Switch kPartSwitches[] = {
    {L"Musica", L"Cartão da música"}, {L"Proxima", L"Próxima da fila"}, {L"Equalizador", L"Equalizador"}, {L"AnelCpu", L"Anel da CPU"},
    {L"AnelRam", L"Anel da RAM"},     {L"AnelGpu", L"Anel da GPU"},     {L"AnelRede", L"Anel da rede"},
};

inline constexpr Switch kFieldSwitches[FieldCount] = {
    {L"JsonUptime", L"uptime"}, {L"JsonData", L"date"}, {L"JsonLuz", L"light_on"}, {L"JsonCpu", L"pc.cpu"},
    {L"JsonGpu", L"pc.gpu"},     {L"JsonRam", L"pc.ram"}, {L"JsonMonitores", L"monitors"},
};

// Tudo o que muda o bloco do canto, lido do registro (HKCU\Software\TemaBarra). parts segue kPartSwitches.
struct WidgetOptions {
    bool parts[std::size(kPartSwitches)] = {true, true, true, true, true, true, true};
    bool fields[FieldCount] = {true, true, true, true, true, true, true};
    bool json = true;
    bool fahrenheit = false;
    bool range = true;
    bool pause = true;
    int position = 2;
    int weatherSide = 2;
    int size = 100;
    int margin = 44;
    int shadow = 75;
    std::wstring hiddenMonitors;

    bool Music() const { return parts[0]; }
    bool Next() const { return parts[1]; }
    bool Equalizer() const { return parts[2]; }
    bool Ring(int i) const { return parts[3 + i]; }
    bool Hidden(std::wstring const& device) const { return hiddenMonitors.find(L"|" + device + L"|") != std::wstring::npos; }
};

WidgetOptions ReadWidgetOptions();
