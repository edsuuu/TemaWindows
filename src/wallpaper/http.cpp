#include "wallpaper/http.h"

#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

// Requisição https://host/caminho com cabeçalhos extras e corpo opcionais; devolve o corpo da resposta, ou "" se
// falhar (sem internet, status diferente de 200 ou ~15 s sem resposta).
std::string HttpsRequest(const wchar_t* host, std::wstring const& path, const wchar_t* method, std::wstring const& headers,
                         std::string const& body) {
    std::string response;
    DWORD status = 0, size = sizeof status;
    HINTERNET session = WinHttpOpen(L"FundoVivo", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    HINTERNET connection = session ? WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0) : nullptr;
    HINTERNET request = connection ? WinHttpOpenRequest(connection, method, path.c_str(), nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE)
                                   : nullptr;

    if (request) WinHttpSetTimeouts(request, 10000, 10000, 15000, 15000);
    if (request &&
        WinHttpSendRequest(request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), DWORD(-1), (LPVOID)body.data(),
                           DWORD(body.size()), DWORD(body.size()), 0) &&
        WinHttpReceiveResponse(request, nullptr) &&
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size, nullptr) && status == 200)
        for (DWORD available = 0, read = 0; WinHttpQueryDataAvailable(request, &available) && available;) {
            size_t offset = response.size();
            response.resize(offset + available);
            if (!WinHttpReadData(request, response.data() + offset, available, &read)) status = 0, read = 0;
            response.resize(offset + read);
            if (!status) break;
        }

    for (HINTERNET handle : {request, connection, session})
        if (handle) WinHttpCloseHandle(handle);
    return status == 200 ? response : "";
}
