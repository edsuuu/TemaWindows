#include "common/glass.h"

#include <cstdio>

#pragma comment(lib, "CoreMessaging.lib")

struct DispatcherQueueOptions {
    DWORD dwSize;
    int threadType;
    int apartmentType;
};

// Declarada à mão: o DispatcherQueue.h briga com os cabeçalhos ABI que o C++/WinRT já puxou.
extern "C" HRESULT WINAPI CreateDispatcherQueueController(DispatcherQueueOptions options, void** controller);

// Monta, num Compositor fora do Explorer, os mesmos grafos de efeito que a barra, os menus e as Configurações usam
// (blur da barra, vidro sobre o backdrop e sobre o host backdrop). Grafo de efeito errado derruba o processo: melhor
// cair aqui do que no Explorer do usuário. Rodar antes de recarregar quando mexer em src\common\glass.cpp ou no blur.
// Sai com 0 e imprime "efeitos ok" se estiver tudo certo. A fila de despacho é da thread atual (2) num STA (2).
int main() {
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    void* controller;
    winrt::check_hresult(CreateDispatcherQueueController({sizeof(DispatcherQueueOptions), 2, 2}, &controller));

    Compositor c;
    EffectOnBackdrop(c, GaussianBlur(3, BackdropSource()), c.CreateBackdropBrush());
    EffectOnBackdrop(c, GlassEffect(24, .5f), c.CreateBackdropBrush());
    EffectOnBackdrop(c, GlassEffect(24, .8f), c.CreateHostBackdropBrush());
    TintedGlass(c);
    puts("efeitos ok");
    return 0;
}
