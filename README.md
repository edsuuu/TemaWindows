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
src\config\     FundoConfig.exe (app dos widgets do FundoVivo: cores, JSON, posições e cidade, com prévia)
src\contextmenu\ TemaContexto.dll (dllhost dos pacotes do menu do clique direito) e o modelo do AppxManifest.xml
build\          build-<projeto>.bat, build-all.bat e build-installer.bat + installer.iss (intermediários em build\obj\, apagados no fim)
bin\            os binários e contexto\ (pacotes gerados do TemaContexto)
assets\         fundo.png (tema golden e papel de parede estático), fundo-morph.bin (tema loop), bloqueio-alga.png, folder.ico (ícone de pasta), contexto.png (logo dos pacotes)
cache\          cache-clima.txt (última resposta do Open-Meteo), contexto\ (lista do menu e ícones das extensões)
logs\           <binário>.log de cada DLL
tools\          reload-*.ps1, theme.ps1, capture.exe, capture-wallpaper.exe, effects-test.exe, folder-icon.ps1 e wallget-icon.ps1 (geram o folder.ico e o wallget.ico), hwinfo-setup.ps1 (usado pelo instalador)
```

Os programas acham `assets\`, `cache\` e `logs\` a partir da pasta acima de `bin\`.

## Componentes

- **TemaBarra** — logo do Iniciar com degradê (branco, cinza e preto ou três cores escolhidas), fundo da barra com blur,
  acrílico ou transparente, indicador de execução cinza, vidro na caixa de
  pesquisa e no painel dos ícones ocultos (^), com o hover dos ícones das pontas acompanhando a curva, sem anel de
  foco. Injeta no Explorer via `InitializeXamlDiagnosticsEx` e reinjeta quando ele reinicia. Relê o fundo e as cores do
  logo a cada segundo, então o app do Wallget muda a barra sem reiniciar o Explorer.
- **TemaMenus** — vidro liso no Iniciar, pesquisa, Configurações Rápidas e notificações; Iniciar no estilo do conceito
  (barra lateral com os botões nativos de conta/energia, Início/Apps/Criar, pastas na largura toda, lupa com degradê).
  Nas Configurações Rápidas: barra de tempo da música no cartão de mídia (só leitura, atualiza só com o painel aberto),
  engrenagem na linha do volume e, nas subpáginas (Wi-Fi, Bluetooth...), Voltar com hover arredondado e interruptor
  à direita.
- **TemaJanelas** — vidro nas Configurações.
- **FundoVivo** — papel de parede animado (temas golden, nbhd e loop, ou uma imagem ou um vídeo do usuário, este
  sem som e em loop pelo Media Foundation) com os widgets no canto: música do Spotify com
  equalizador, anéis de CPU/RAM/GPU/rede com temperaturas (HWiNFO e NVML), clima e um JSON com uptime, data e hora,
  specs e monitores. Pausa com jogo, tela cheia ou tela bloqueada (o overlay do UnkvoidClips não conta). Põe o ícone
  com degradê nas pastas da área de trabalho, inclusive nas novas (`Pastas` = 0 desliga; pula repositórios git).
  Botões aleatória ⏮ ⏯ ⏭ clicáveis no cartão da música: o clique cai em janelas invisíveis (em camada, alfa 1)
  filhas do Progman, acima dos ícones, e vira comando nos controles de mídia do Windows (sem hook de mouse). A
  aleatória só liga/desliga (a ordem inteligente do Spotify não passa pelos controles do Windows nem pela API). Nome da
  música e do artista sublinham no hover e, no clique, abrem a música ou o artista no Spotify (link exato com o login
  do Spotify feito; sem ele, pela busca). Cores, partes, JSON, posição (9 lugares), tamanho, monitores, cidade do clima e
  fundo vêm do FundoConfig. Fica na bandeja (ícones ocultos, perto do relógio): clique abre o app; o menu tem Abrir e
  Sair.
- **TemaContexto** — põe no menu novo do clique direito os itens que só apareciam em "Mostrar mais opções" (Git,
  Visual Studio, PhpStorm, 7-Zip, NVIDIA e o que for instalado depois) e o item "Abrir WSL" (Terminal com o PowerShell
  padrão já no WSL, na home). Lê os verbos (`shell\`) e as extensões de menu (`shellex\ContextMenuHandlers`) de
  terceiros no registro, pulando o que é do Windows. O menu novo só aceita itens de pacote e junta num submenu os de
  pacotes com o mesmo nome, então são 11 pacotes com local externo e nome próprio (10 posições + Abrir WSL), sem
  assinatura (precisa do Modo Desenvolvedor). Cada pacote roda num dllhost próprio, que o vigia
  (`rundll32 TemaContexto.dll,Run`, no `HKCU\...\Run`) deixa aberto; sem isso o menu mostra "Carregando" e encolhe. Só
  a posição 0 pergunta a seleção ao Explorer e monta a lista; as outras leem de `cache\contexto\lista.txt`. A extensão
  da NVIDIA não carrega fora do Explorer, então o item dela abre o app direto. O próprio "Mostrar mais opções" some do
  menu: o menu novo é WinUI 3 (não o XAML do sistema que o TemaBarra usa), e o vigia injeta no Explorer um TAP de
  diagnóstico do WinUI 3 (`menu_tap.cpp`, pelo `Microsoft.Internal.FrameworkUdk.dll`) que esconde o botão
  `expandtoclassic` e o separador acima dele. O menu antigo continua no Shift + clique direito, com os itens duas
  vezes (o original e o do pacote). Trocar o TAP pede reiniciar o Explorer (a DLL fica carregada nele).

Autostart: `HKCU\...\Run` (`TemaBarra`, `TemaMenus`, `TemaJanelas`, `TemaContexto`, via `rundll32 bin\<dll>,Run`) e `HKLM\...\Run`
(`FundoVivo`, para todos os usuários). Tela de bloqueio: `HKLM\...\PersonalizationCSP` aponta para
`assets\bloqueio-alga.png`.

## Wallget (instalador dos widgets)

`build\build-installer.bat` compila o FundoVivo e o FundoConfig e gera `bin\Wallget-Setup.exe` (Inno Setup 6:
`winget install JRSoftware.InnoSetup --scope user`). Instala por usuário em `%LOCALAPPDATA%\Programs\Wallget` como
`Wallget.exe` (o papel de parede, no `HKCU\...\Run` já na primeira vez que abre, a não ser que o app desligue) e
`WallgetConfig.exe` (atalho "Wallget" no Iniciar). A opção "Barra de tarefas" (desmarcada) instala também a
`TemaBarra.dll`, se não houver outra iniciando com o Windows; numa atualização a DLL nova entra no próximo login. Se faltar o
HWiNFO (temperatura da CPU) ou a tarefa que reabre ele a cada 11 h (a memória compartilhada da versão grátis desliga
em 12 h), oferece instalar com `tools\hwinfo-setup.ps1`, o único passo que pede administrador.

No app, cada mudança já vale no papel de parede e na barra (o FundoVivo e a TemaBarra releem o registro a cada
segundo); o Salvar mantém, e fechar sem salvar volta tudo ao que estava.

## Liga/desliga

Valores em `HKCU\Software\TemaBarra` (DWORD, ligado quando não existe). Para ver e mudar:

```powershell
tools\theme.ps1                         # estado de tudo
tools\theme.ps1 off Fundo -Reload       # desliga e recarrega o componente
tools\theme.ps1 set FundoTema loop      # o tema do fundo vale na hora
```

Textos (REG_SZ) que ficam fora do código:

- `ClimaLatitude` e `ClimaLongitude` — lugar do clima (ex.: `-23.55` e `-46.63`); sem eles, o painel fica sem clima.
  `ClimaCidade` é só o nome mostrado no FundoConfig, que escreve os três ao escolher uma cidade.
- `CorTexto`, `CorTextoSuave`, `CorAnel`, `CorEqualizador`, `CorPico`, `CorJsonChave`, `CorJsonValor`,
  `CorJsonPontuacao` — cores dos widgets (`#RRGGBB`); sem o valor, a cor padrão.
- `FundoTema` — `golden`, `nbhd`, `loop`, `imagem` ou `video`; `FundoImagem` e `FundoVideo` — caminho do arquivo.
- `MonitoresOcultos` — monitores sem o bloco, pelo nome GDI (`|\\.\DISPLAY2|`).
- `LogoCor1`, `LogoCor2`, `LogoCor3` — degradê do logo do Iniciar (topo à esquerda, meio, embaixo à direita);
  sem eles, branco, cinza e preto.
- `SpotifyClientId` — Client ID do app do Spotify para a próxima música da fila (login uma vez com
  `tools\spotify-login.ps1`; o token fica criptografado em `cache\`).

Luz do quarto (`"light_on": true/false` no JSON, clique no valor): interruptor Tuya / Smart Life controlado
pela rede local (protocolo 3.3, porta 6668), sem nuvem. Configure uma vez com `tools\tuya-setup.ps1`, que busca a
chave local na API da Tuya e guarda id + chave + IP criptografados em `cache\tuya-quarto.bin`; sem esse arquivo, a
linha não aparece.

Números (DWORD) dos widgets e do fundo:

- Partes (0 esconde): `Musica`, `Proxima`, `Equalizador`, `AnelCpu`, `AnelRam`, `AnelGpu`, `AnelRede`; o JSON inteiro
  (`Json`) e cada campo dele (`JsonUptime`, `JsonData`, `JsonLuz`, `JsonCpu`, `JsonGpu`, `JsonRam`, `JsonMonitores`).
- `Posicao` (0 a 8, da esquerda para a direita e de cima para baixo; padrão 2, em cima à direita), `PosicaoClima` (0 à
  esquerda do JSON, 1 em cima, 2 à direita), `Escala` (50 a 200 %), `Margem` (px da borda, padrão 44) e `Sombra`
  (0 a 100 %, padrão 75).
- `ClimaUnidade` (1 = °F), `ClimaMaxMin` (0 esconde máxima e mínima), `FundoPausa` (0 não pausa com jogo ou tela
  cheia), `FundoOndular` (1 ondula a imagem como o golden), `IniciarComWindows` (0 = o Wallget não se põe no Run).
- Barra: `BlurModo` (0 acrílico, 1 blur, 2 transparente), `BlurRaio` (px do blur, padrão 9) e `BlurVeu` (0 a 255,
  véu preto por cima; padrão 31).

## Compilar e recarregar

Precisa do VS 2022 Build Tools (C++ x64). `build\build-all.bat` compila tudo; cada `build\build-<projeto>.bat`
compila um. Para trocar o que está rodando, use o script do componente (com `-Build` ele compila antes):

| Componente | Script | Efeito colateral |
|---|---|---|
| TemaBarra | `tools\reload-taskbar.ps1 [-Build] [-Shot x.png]` | reinicia o Explorer (reabre as pastas que estavam abertas) |
| TemaMenus | `tools\reload-menus.ps1 [-Build]` | derruba os hosts do Iniciar e dos painéis (o Windows relança) |
| TemaJanelas | `tools\reload-settings.ps1 [-Build]` | Configurações já abertas mantêm a DLL velha até reabrir |
| FundoVivo | `tools\reload-wallpaper.ps1 [-Build] [-Shot x.png]` | nenhum |
| TemaContexto | `tools\reload-contextmenu.ps1 [-Build] [-Remove]` | registra os pacotes de novo e fecha os dllhost com a DLL velha |

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
