#pragma once

#include <windows.h>
#include <vector>

using NvmlTemperatureFn = int (*)(void*, int, unsigned*);

struct SystemStats {
    double cpu = 0;
    double ram = 0;
    double ramGb = 0;
    double gpu = 0;
    double gpuTemperature = -1;
    double cpuTemperature = -1;

    explicit SystemStats(LUID adapter);
    void Read();

private:
    LUID adapter{};
    NvmlTemperatureFn nvmlTemperature = nullptr;
    void* nvmlDevice = nullptr;
    ULONGLONG previousIdle = 0;
    ULONGLONG previousTotal = 0;
    ULONGLONG previousGpuTime = 0;
    std::vector<ULONGLONG> previousNodeTimes;

    void ReadCpuAndRam();
    void ReadGpuUsage();
};

double ReadHwinfoCpuTemperature();
