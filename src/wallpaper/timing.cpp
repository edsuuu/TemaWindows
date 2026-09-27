#include "wallpaper/timing.h"

// Segundos desde um ponto qualquer, com precisão de microssegundos.
double Now() {
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return double(counter.QuadPart) / frequency.QuadPart;
}

// FILETIME como um número de 64 bits (unidades de 100 ns).
ULONGLONG ToU64(FILETIME time) {
    return (ULONGLONG)time.dwHighDateTime << 32 | time.dwLowDateTime;
}
