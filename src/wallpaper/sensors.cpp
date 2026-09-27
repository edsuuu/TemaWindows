#include "wallpaper/sensors.h"
#include "wallpaper/timing.h"

#include <winternl.h>
#include <d3dkmthk.h>
#include <algorithm>

// Abre o NVML (vem com o driver da NVIDIA; só lê) para a temperatura da GPU e faz a primeira leitura, que serve de
// base para as diferenças de CPU e GPU.
SystemStats::SystemStats(LUID adapter) : adapter(adapter) {
    if (HMODULE nvml = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        auto init = (int (*)())GetProcAddress(nvml, "nvmlInit_v2");
        auto deviceByIndex = (int (*)(unsigned, void**))GetProcAddress(nvml, "nvmlDeviceGetHandleByIndex_v2");
        nvmlTemperature = (NvmlTemperatureFn)GetProcAddress(nvml, "nvmlDeviceGetTemperature");
        if (!init || !deviceByIndex || init() || deviceByIndex(0, &nvmlDevice)) nvmlTemperature = nullptr;
    }
    Read();
}

// CPU pelo tempo ocioso desde a última leitura (GetSystemTimes; o tempo de kernel já inclui o ocioso) e RAM em uso.
void SystemStats::ReadCpuAndRam() {
    FILETIME idleTime, kernelTime, userTime;
    GetSystemTimes(&idleTime, &kernelTime, &userTime);

    ULONGLONG idle = ToU64(idleTime), total = ToU64(kernelTime) + ToU64(userTime);
    cpu = previousTotal && total > previousTotal ? 100.0 * (1 - double(idle - previousIdle) / (total - previousTotal)) : 0;
    previousIdle = idle;
    previousTotal = total;

    MEMORYSTATUSEX memory{sizeof memory};
    GlobalMemoryStatusEx(&memory);
    ram = memory.dwMemoryLoad;
    ramGb = (memory.ullTotalPhys - memory.ullAvailPhys) / 1073741824.0;
}

// GPU igual ao Gerenciador de Tarefas: o tempo ocupado do motor mais ocupado desde a última leitura.
void SystemStats::ReadGpuUsage() {
    D3DKMT_QUERYSTATISTICS query{};
    query.Type = D3DKMT_QUERYSTATISTICS_ADAPTER;
    query.AdapterLuid = adapter;
    if (!NT_SUCCESS(D3DKMTQueryStatistics(&query))) return;

    UINT nodes = query.QueryResult.AdapterInformation.NodeCount;
    FILETIME now;
    GetSystemTimePreciseAsFileTime(&now);
    double span = double(ToU64(now) - previousGpuTime), busiest = 0;
    previousNodeTimes.resize(nodes);

    for (UINT i = 0; i < nodes; i++) {
        query = {};
        query.Type = D3DKMT_QUERYSTATISTICS_NODE;
        query.AdapterLuid = adapter;
        query.QueryNode.NodeId = i;
        if (!NT_SUCCESS(D3DKMTQueryStatistics(&query))) continue;

        ULONGLONG running = query.QueryResult.NodeInformation.GlobalInformation.RunningTime.QuadPart;
        if (previousGpuTime && previousNodeTimes[i]) busiest = std::max(busiest, (running - previousNodeTimes[i]) / span);
        previousNodeTimes[i] = running;
    }

    gpu = previousGpuTime ? std::min(busiest * 100, 100.0) : 0;
    previousGpuTime = ToU64(now);
}

// Uma leitura de tudo: uso de CPU, RAM e GPU e as temperaturas (GPU pelo NVML, CPU pelo HWiNFO; -1 = sem leitura).
void SystemStats::Read() {
    ReadCpuAndRam();
    ReadGpuUsage();

    unsigned temperature;
    gpuTemperature = nvmlTemperature && !nvmlTemperature(nvmlDevice, 0, &temperature) ? temperature : -1;
    cpuTemperature = ReadHwinfoCpuTemperature();
}
