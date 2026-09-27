#pragma once

#include <string>
#include <vector>

struct PcSpecs {
    std::wstring cpu;
    std::wstring gpu;
    std::wstring ram;
};

PcSpecs ReadPcSpecs();
std::vector<std::wstring> ReadMonitors();
