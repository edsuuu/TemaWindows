# TemaWindows

Tema preto/cinza do Windows 11 feito sob medida: barra de tarefas, Iniciar e painéis, Configurações e papel de parede
animado. Tudo roda como o usuário, sem driver e sem injeção global (só nos processos listados abaixo).

## Estrutura

```
src\common\     código das três DLLs: log, registro, caminhos, injeção XAML (TAP), vidro/efeitos, árvore XAML
src\taskbar\    TemaBarra.dll   (explorer.exe)
src\menus\      TemaMenus.dll   (StartMenuExperienceHost, SearchHost, ShellHost, ShellExperienceHost)
src\settings\   TemaJanelas.dll (SystemSettings)
src\wallpaper\  FundoVivo.exe   (janela filha do WorkerW, atrás dos ícones da área de trabalho)
build\          build-<projeto>.bat e build-all.bat (intermediários em build\obj\, apagados no fim)
bin\            os quatro binários
assets\         fundo.png (tema golden e papel de parede estático), fundo-morph.bin (tema loop), bloqueio-alga.png, folder.ico (ícone de pasta)
cache\          cache-clima.txt (última resposta do Open-Meteo)
logs\           <binário>.log de cada DLL
tools\          reload-*.ps1, theme.ps1, capture.exe, capture-wallpaper.exe, effects-test.exe, folder-icon.ps1 (gera o folder.ico)
```

Os programas acham `assets\`, `cache\` e `logs\` a partir da pasta acima de `bin\`.

## Componentes

- **TemaBarra** — logo do Iniciar com degradê, blur no fundo da barra, indicador de execução cinza, vidro na caixa de
  pesquisa e no painel dos ícones ocultos (^), com o hover dos ícones das pontas acompanhando a curva, sem anel de
  foco. Injeta no Explorer via `InitializeXamlDiagnosticsEx` e reinjeta quando ele reinicia.
- **TemaMenus** — vidro liso no Iniciar, pesquisa, Configurações Rápidas e notificações; Iniciar no estilo do conceito
  (barra lateral com os botões nativos de conta/energia, Início/Apps/Criar, pastas na largura toda, lupa com degradê).
  Nas Configurações Rápidas: barra de tempo da música no cartão de mídia (só leitura, atualiza só com o painel aberto),
  engrenagem na linha do volume e, nas subpáginas (Wi-Fi, Bluetooth...), Voltar com hover arredondado e interruptor
  à direita.
- **TemaJanelas** — vidro nas Configurações.
- **FundoVivo** — papel de parede animado (temas golden, nbhd e loop) com os widgets no canto: música do Spotify com
  equalizador, anéis de CPU/RAM/GPU/rede com temperaturas (HWiNFO e NVML), clima e um JSON com uptime, data e hora,
  specs e monitores. Pausa com jogo, tela cheia ou tela bloqueada (o overlay do UnkvoidClips não conta). Põe o ícone
  com degradê nas pastas da área de trabalho, inclusive nas novas (`Pastas` = 0 desliga; pula repositórios git).
  Botões ⏮ ⏯ ⏭ clicáveis no cartão da música: o clique cai em janelas invisíveis (em camada, alfa 1) filhas do
  Progman, acima dos ícones, e vira comando nos controles de mídia do Windows (sem hook de mouse).

Autostart: `HKCU\...\Run` (`TemaBarra`, `TemaMenus`, `TemaJanelas`, via `rundll32 bin\<dll>,Run`) e `HKLM\...\Run`
(`FundoVivo`, para todos os usuários). Tela de bloqueio: `HKLM\...\PersonalizationCSP` aponta para
`assets\bloqueio-alga.png`.

## Liga/desliga

Valores em `HKCU\Software\TemaBarra` (DWORD, ligado quando não existe). Para ver e mudar:

```powershell
tools\theme.ps1                         # estado de tudo
tools\theme.ps1 off Fundo -Reload       # desliga e recarrega o componente
tools\theme.ps1 set FundoTema loop      # o tema do fundo vale na hora
```

## Compilar e recarregar

Precisa do VS 2022 Build Tools (C++ x64). `build\build-all.bat` compila tudo; cada `build\build-<projeto>.bat`
compila um. Para trocar o que está rodando, use o script do componente (com `-Build` ele compila antes):

| Componente | Script | Efeito colateral |
|---|---|---|
| TemaBarra | `tools\reload-taskbar.ps1 [-Build] [-Shot x.png]` | reinicia o Explorer (reabre as pastas que estavam abertas) |
| TemaMenus | `tools\reload-menus.ps1 [-Build]` | derruba os hosts do Iniciar e dos painéis (o Windows relança) |
| TemaJanelas | `tools\reload-settings.ps1 [-Build]` | Configurações já abertas mantêm a DLL velha até reabrir |
| FundoVivo | `tools\reload-wallpaper.ps1 [-Build] [-Shot x.png]` | nenhum |

## Cuidados

- Nada de compilar ou recarregar com o jogo aberto (`UAGame`); os scripts se recusam.
- As DLLs rodam dentro do Explorer, do Iniciar e dos painéis: um erro derruba a barra ou o menu. Mexeu em
  `src\common\glass.cpp` ou no blur? Rode `tools\effects-test.exe` (tem que imprimir "efeitos ok") antes de recarregar.
- Reiniciar o Explorer fecha janelas de pasta e às vezes um app já aberto perde o botão da barra (bug do Windows):
  feche e abra o app de novo.
- Prints: use `tools\capture.exe <saida.png> [monitor]` (DXGI; mostra blur e acrílico, que o CopyFromScreen não pega)
  ou `tools\capture-wallpaper.exe <saida.png>` para o papel de parede inteiro. Com a tela bloqueada não há quadro.
- Os hosts do Iniciar e dos painéis são AppContainer: os builds dão leitura em `bin\*.dll` e escrita no log para
  "todos os pacotes de aplicativo"; a chave `HKCU\Software\TemaBarra` também tem leitura para eles.
