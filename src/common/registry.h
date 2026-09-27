#pragma once

#include <windows.h>
#include <string>

DWORD Setting(const wchar_t* name, DWORD fallback);
bool Enabled(const wchar_t* name);
std::wstring SettingText(const wchar_t* name, const wchar_t* fallback);
