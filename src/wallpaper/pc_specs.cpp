#include "wallpaper/pc_specs.h"

#include <windows.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <vector>

using Microsoft::WRL::ComPtr;

// Tira todas as ocorrências de `piece` do texto.
static void Remove(std::wstring& text, const wchar_t* piece) {
    for (size_t p; (p = text.find(piece)) != std::wstring::npos;) text.erase(p, wcslen(piece));
}

// Nome do processador pelo registro, enxuto: "13th Gen Intel(R) Core(TM) i7-13700K" -> "Intel Core i7-13700K";
// "AMD Ryzen 7 7800X3D 8-Core Processor" -> "AMD Ryzen 7 7800X3D".
static std::wstring CpuName() {
    wchar_t value[128] = L"";
    DWORD size = sizeof value;
    RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString", RRF_RT_REG_SZ, nullptr,
                 value, &size);

    std::wstring cpu = value;
    for (auto piece : {L"(R)", L"(TM)", L" CPU", L" Processor"}) Remove(cpu, piece);
    if (size_t p = cpu.find(L" @"); p != std::wstring::npos) cpu.erase(p);
    if (size_t p = cpu.find(L"Gen "); p != std::wstring::npos) cpu.erase(0, p + 4);
    if (size_t p = cpu.find(L"-Core"); p != std::wstring::npos) cpu.erase(cpu.rfind(L' ', p), p + 5 - cpu.rfind(L' ', p));
    for (size_t p; (p = cpu.find(L"  ")) != std::wstring::npos;) cpu.erase(p, 1);
    cpu.erase(0, cpu.find_first_not_of(L' '));
    return cpu;
}

// Placa de vídeo principal (DXGI) com a memória dedicada: "NVIDIA GeForce RTX 4060 Ti" -> "RTX 4060 Ti 8 GB".
static std::wstring GpuName() {
    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 desc;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumAdapters1(0, &adapter)) || FAILED(adapter->GetDesc1(&desc)))
        return L"";

    std::wstring gpu = desc.Description;
    for (auto piece : {L"NVIDIA ", L"GeForce ", L"AMD ", L"(R)", L"(TM)"}) Remove(gpu, piece);

    wchar_t memory[32];
    swprintf_s(memory, L" %llu GB", (unsigned long long)((desc.DedicatedVideoMemory + (1ull << 29)) >> 30));
    return gpu + memory;
}

// Tipo e velocidade da RAM pela tabela SMBIOS, tipo 17 ("Memory Device") do primeiro pente presente: tipo no byte
// 0x12 e velocidade configurada em 0x20 (sem ela, a nominal em 0x15). As strings de cada estrutura terminam em dois
// zeros.
static std::wstring RamDetails() {
    UINT size = GetSystemFirmwareTable('RSMB', 0, nullptr, 0);
    std::vector<BYTE> table(size);
    if (size <= 8 || GetSystemFirmwareTable('RSMB', 0, table.data(), size) != size) return L"";

    const BYTE* p = table.data() + 8;
    const BYTE* end = table.data() + std::min<size_t>(size, 8 + *(const DWORD*)(table.data() + 4));
    while (p + 4 <= end && p[0] != 127) {
        if (p[0] == 17 && p[1] >= 0x17 && p + p[1] <= end && *(const WORD*)(p + 0x0C)) {
            int type = p[0x12];
            int speed = p[1] >= 0x22 && *(const WORD*)(p + 0x20) ? *(const WORD*)(p + 0x20) : *(const WORD*)(p + 0x15);
            const wchar_t* name = type == 0x18 ? L"DDR3" : type == 0x1A ? L"DDR4" : type == 0x1E ? L"LPDDR4" : type == 0x22 ? L"DDR5"
                                : type == 0x23 ? L"LPDDR5" : nullptr;

            wchar_t details[64] = L"";
            if (name) swprintf_s(details, L" %s", name);
            if (speed > 0 && speed < 0xFFFF) swprintf_s(details + wcslen(details), 64 - wcslen(details), L" %d", speed);
            return details;
        }

        const BYTE* strings = p + p[1];
        while (strings + 1 < end && (strings[0] || strings[1])) strings++;
        p = strings + 2;
    }
    return L"";
}

// Monitores ativos da esquerda para a direita: nome do EDID, resolução e taxa de atualização ("AW2525HM 1920x1080 240Hz").
std::vector<std::wstring> ReadMonitors() {
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return {};

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS) return {};

    std::vector<std::pair<LONG, std::wstring>> sorted;
    for (UINT32 i = 0; i < pathCount; i++) {
        auto const& path = paths[i];
        if (path.sourceInfo.modeInfoIdx >= modeCount) continue;

        DISPLAYCONFIG_TARGET_DEVICE_NAME name{};
        name.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof name, path.targetInfo.adapterId, path.targetInfo.id};
        if (DisplayConfigGetDeviceInfo(&name.header) != ERROR_SUCCESS || !name.monitorFriendlyDeviceName[0])
            wcscpy_s(name.monitorFriendlyDeviceName, L"Monitor");

        auto const& source = modes[path.sourceInfo.modeInfoIdx].sourceMode;
        auto rate = path.targetInfo.refreshRate;
        double hertz = rate.Denominator ? double(rate.Numerator) / rate.Denominator : 0;

        wchar_t text[128];
        swprintf_s(text, L"%s %ux%u %.0fHz", name.monitorFriendlyDeviceName, source.width, source.height, hertz);
        sorted.push_back({source.position.x, text});
    }

    std::sort(sorted.begin(), sorted.end());
    std::vector<std::wstring> monitors;
    for (auto& [x, text] : sorted) monitors.push_back(std::move(text));
    return monitors;
}

// Specs do PC para o JSON do painel: processador, placa de vídeo e RAM (tamanho + tipo/velocidade).
PcSpecs ReadPcSpecs() {
    ULONGLONG kilobytes = 0;
    GetPhysicallyInstalledSystemMemory(&kilobytes);

    wchar_t ram[32];
    swprintf_s(ram, L"%llu GB", (kilobytes + 524288) / 1048576);
    return {CpuName(), GpuName(), ram + RamDetails()};
}
