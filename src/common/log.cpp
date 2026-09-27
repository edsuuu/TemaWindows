#include "common/log.h"
#include "common/paths.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <share.h>

// Arquivo de log do componente: logs\<nome do módulo>.log na raiz do projeto.
static std::wstring const& LogPath() {
    static const std::wstring path = ProjectPath(L"logs\\" + ModuleName() + L".log");
    return path;
}

// Acrescenta um texto pronto ao log. Vários processos escrevem juntos (os hosts do Iniciar e dos painéis), então o
// arquivo abre compartilhado; sem acesso a ele, o texto vai para o depurador.
void LogRaw(std::wstring const& text) {
    FILE* file = _wfsopen(LogPath().c_str(), L"a, ccs=UTF-8", _SH_DENYNO);
    if (!file) {
        OutputDebugStringW(text.c_str());
        return;
    }

    fputws(text.c_str(), file);
    fclose(file);
}

// Uma linha no log com o PID na frente. Mensagem longa demais é cortada em vez de derrubar o processo hospedeiro.
void Log(const wchar_t* format, ...) {
    wchar_t message[1024];
    wchar_t line[1100];
    va_list args;

    va_start(args, format);
    _vsnwprintf_s(message, _TRUNCATE, format, args);
    va_end(args);

    swprintf_s(line, L"[%lu] %s\n", GetCurrentProcessId(), message);
    LogRaw(line);
}
