#pragma once

#include <string>
#include <string_view>

std::wstring ModulePath();
std::wstring ModuleName();
std::wstring ProjectPath(std::wstring_view relative);
