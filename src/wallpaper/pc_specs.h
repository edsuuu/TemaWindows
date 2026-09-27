#pragma once

#include <string>

struct PcSpecs {
    std::wstring cpu;
    std::wstring gpu;
    std::wstring ram;
};

PcSpecs ReadPcSpecs();
