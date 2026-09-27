#include <winsock2.h>
#include "wallpaper/network.h"
#include "wallpaper/timing.h"

#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <algorithm>
#include <cstdio>

#pragma comment(lib, "iphlpapi.lib")

// Mbps com vírgula: uma casa decimal até 99,9, inteiro acima.
static std::wstring FormatMbps(double value) {
    wchar_t text[32];
    swprintf_s(text, value < 99.95 ? L"%.1f" : L"%.0f", value);

    std::wstring result = text;
    for (auto& c : result)
        if (c == L'.') c = L',';
    return result;
}

// Bytes recebidos e enviados, somando só as placas físicas ligadas: sem loopback, virtuais (Hyper-V, VirtualBox, WAN)
// e filtros (repetem a contagem).
static void CountBytes(ULONG64& in, ULONG64& out) {
    MIB_IF_TABLE2* table;
    if (GetIfTable2(&table) != NO_ERROR) return;

    for (ULONG i = 0; i < table->NumEntries; i++) {
        auto& row = table->Table[i];
        auto flags = row.InterfaceAndOperStatusFlags;
        if (flags.HardwareInterface && !flags.FilterInterface && row.OperStatus == IfOperStatusUp) {
            in += row.InOctets;
            out += row.OutOctets;
        }
    }
    FreeMibTable(table);
}

// Lida a cada 1,5 s, só com o fundo visível: atualiza os textos ↓/↑ em Mbps e devolve o % do anel, que é o download em
// relação ao pico recente (o pico cai pela metade em ~3,5 min e nunca fica abaixo de 10 Mbps). Depois de uma pausa só
// pega a base de novo. O espaço fino depois da seta evita que o "↑" colado pareça um "1" em 12 px.
double NetworkMeter::Read() {
    ULONG64 in = 0, out = 0;
    CountBytes(in, out);

    double now = Now(), dt = now - previousTime, download = 0, upload = 0;
    if (dt > 0 && dt < 5 && in >= previousIn && out >= previousOut) {
        download = (in - previousIn) * 8 / dt / 1e6;
        upload = (out - previousOut) * 8 / dt / 1e6;
    }
    previousIn = in;
    previousOut = out;
    previousTime = now;

    peak = std::max({download, peak * 0.995, 10.0});
    down = L"\u2193\u2009" + FormatMbps(download);
    up = L"\u2191\u2009" + FormatMbps(upload);
    return 100 * download / peak;
}
