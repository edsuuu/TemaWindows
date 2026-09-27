#include "wallpaper/sensors.h"

#include <cstring>
#include <string>

// Procura a leitura "CPU Package" na memória compartilhada do HWiNFO (cabeçalho 'HWiS'): a do sensor interno do
// processador ("...: DTS") ou, sem ela, a primeira. Não confia cegamente nos tamanhos: tudo tem que caber na região.
static double FindCpuPackage(const BYTE* shared, size_t regionSize) {
    auto u32 = [&](size_t offset) { return *(const UINT*)(shared + offset); };
    if (regionSize < 44 || u32(0) != 0x53695748) return -1;

    size_t sensorOffset = u32(20), sensorSize = u32(24), sensorCount = u32(28);
    size_t readingOffset = u32(32), readingSize = u32(36), readingCount = u32(40);
    if (sensorSize < 264 || readingSize < 292 || sensorOffset + sensorCount * sensorSize > regionSize ||
        readingOffset + readingCount * readingSize > regionSize)
        return -1;

    double value = -1;
    for (size_t i = 0; i < readingCount; i++) {
        const BYTE* reading = shared + readingOffset + i * readingSize;
        bool temperature = u32(readingOffset + i * readingSize) == 1;
        if (!temperature || strncmp((const char*)reading + 12, "CPU Package", 128)) continue;

        size_t sensor = *(const UINT*)(reading + 4);
        bool dts = sensor < sensorCount && strstr(std::string((const char*)shared + sensorOffset + sensor * sensorSize + 8, 127).c_str(), "DTS");
        if (dts || value < 0) value = *(const double*)(reading + 284);
        if (dts) break;
    }
    return value;
}

// Temperatura da CPU pela memória compartilhada do HWiNFO, que é quem lê o sensor (nada de driver de kernel aqui, por
// causa do anti-cheat). Sem HWiNFO fica -1 ("--°"). Abre a cada leitura: se o HWiNFO reiniciar, não fica preso num
// mapeamento velho.
double ReadHwinfoCpuTemperature() {
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Global\\HWiNFO_SENS_SM2");
    if (!mapping) return -1;

    double value = -1;
    if (auto* shared = (const BYTE*)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0)) {
        MEMORY_BASIC_INFORMATION region{};
        VirtualQuery(shared, &region, sizeof region);
        value = FindCpuPackage(shared, region.RegionSize);
        UnmapViewOfFile(shared);
    }

    CloseHandle(mapping);
    return value;
}
