#pragma once

#include <string>

std::string HttpsRequest(const wchar_t* host, std::wstring const& path, const wchar_t* method = L"GET", std::wstring const& headers = L"",
                         std::string const& body = "");
