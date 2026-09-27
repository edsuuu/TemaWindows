#include "common/registry.h"

static const wchar_t* const kKey = L"Software\\TemaBarra";

// Lê um DWORD de HKCU\Software\TemaBarra; sem o valor, fica o padrão.
DWORD Setting(const wchar_t* name, DWORD fallback) {
    DWORD value = fallback;
    DWORD size = sizeof value;

    RegGetValueW(HKEY_CURRENT_USER, kKey, name, RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value;
}

// Liga/desliga de um recurso: DWORD em HKCU\Software\TemaBarra, ligado quando não existe.
bool Enabled(const wchar_t* name) {
    return Setting(name, 1) != 0;
}

// Lê um texto de HKCU\Software\TemaBarra; sem o valor (ou comprido demais), fica o padrão.
std::wstring SettingText(const wchar_t* name, const wchar_t* fallback) {
    wchar_t value[64];
    DWORD size = sizeof value;

    if (RegGetValueW(HKEY_CURRENT_USER, kKey, name, RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS) return fallback;
    return value;
}
