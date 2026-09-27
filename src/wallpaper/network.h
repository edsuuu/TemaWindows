#pragma once

#include <windows.h>
#include <string>

struct NetworkMeter {
    std::wstring down = L"\u2193\u2009" L"0,0";
    std::wstring up = L"\u2191\u2009" L"0,0";

    double Read();

private:
    ULONG64 previousIn = 0;
    ULONG64 previousOut = 0;
    double previousTime = 0;
    double peak = 10;
};
