# Liga, desliga e ajusta os recursos do tema (valores em HKCU\Software\TemaBarra). Sem argumentos mostra o estado de
# tudo. A maioria só vale depois de recarregar o componente: -Reload chama o tools\reload-<componente>.ps1 dele
# (o da barra reinicia o Explorer e fecha as janelas de pasta).
# Uso: .\theme.ps1
#      .\theme.ps1 on  <Nome> [-Reload]
#      .\theme.ps1 off <Nome> [-Reload]
#      .\theme.ps1 set <Nome> <Valor> [-Reload]
param([ValidateSet('list', 'on', 'off', 'set')][string]$Action = 'list', [string]$Name, [string]$Value, [switch]$Reload)
$ErrorActionPreference = 'Stop'
$Key = 'HKCU:\Software\TemaBarra'

$Features = @(
    @{ Name = 'Logo'; Default = 1; Parts = 'taskbar'; About = 'logo do Iniciar quadrado com degradê' }
    @{ Name = 'Fundo'; Default = 1; Parts = 'taskbar'; About = 'blur no fundo da barra; botão "mostrar área de trabalho" invisível' }
    @{ Name = 'Vidro'; Default = 1; Parts = 'taskbar'; About = 'vidro na caixa de pesquisa e no painel dos ícones ocultos (^)' }
    @{ Name = 'BlurModo'; Default = 1; Parts = 'taskbar'; About = '0 = acrílico forte (host backdrop); 1 = gaussiano leve' }
    @{ Name = 'BlurRaio'; Default = 9; Parts = 'taskbar'; About = 'raio do blur gaussiano da barra (px)' }
    @{ Name = 'BlurVeu'; Default = 31; Parts = 'taskbar'; About = 'alfa do véu preto por cima do blur (0-255)' }
    @{ Name = 'VidroTinta'; Default = 80; Parts = 'taskbar menus'; About = 'opacidade da camada cinza do vidro (0-100)' }
    @{ Name = 'VidroBlur'; Default = 24; Parts = 'taskbar menus settings'; About = 'desfoque do vidro (DIPs; 0 = só a camada)' }
    @{ Name = 'VidroRaio'; Default = 20; Parts = 'taskbar menus'; About = 'cantos dos painéis de vidro (DIPs)' }
    @{ Name = 'Menus'; Default = 1; Parts = 'menus'; About = 'vidro no Iniciar, pesquisa, Configurações Rápidas e notificações' }
    @{ Name = 'MenusConceito'; Default = 1; Parts = 'menus'; About = 'layout do conceito no Iniciar (textos, seções em cartões)' }
    @{ Name = 'MenusLateral'; Default = 1; Parts = 'menus'; About = 'barra lateral do Iniciar (conta, Início/Apps/Criar, energia)' }
    @{ Name = 'MenusDump'; Default = 0; Parts = 'menus'; About = 'descoberta: incrementar MenusDumpNow despeja a árvore XAML no log' }
    @{ Name = 'Janelas'; Default = 1; Parts = 'settings'; About = 'vidro nas Configurações' }
    @{ Name = 'JanelasTinta'; Default = 80; Parts = 'settings'; About = 'opacidade da camada do vidro das Configurações (0-100)' }
    @{ Name = 'FundoVivo'; Default = 1; Parts = 'wallpaper'; About = 'papel de parede animado com os widgets' }
    @{ Name = 'FundoTema'; Default = 'nbhd'; Parts = 'wallpaper'; About = 'tema do fundo: golden | nbhd | loop (vale na hora)' }
)

# Mostra cada recurso com o valor atual (ou o padrão, se não estiver no registro).
function Show-Features {
    $current = Get-ItemProperty $Key -ErrorAction SilentlyContinue
    $Features | ForEach-Object {
        $value = $current.($_.Name)
        [pscustomobject]@{
            Nome       = $_.Name
            Valor      = if ($null -eq $value) { "$($_.Default) (padrão)" } else { $value }
            Componente = $_.Parts
            Recurso    = $_.About
        }
    } | Format-Table -AutoSize | Out-String -Width 200
}

# Grava o valor (FundoTema é texto; o resto, DWORD) e, se pedido, recarrega os componentes que o leem.
function Set-Feature([string]$FeatureName, [string]$NewValue) {
    $feature = $Features | Where-Object { $_.Name -eq $FeatureName }
    if (-not $feature) { throw "Recurso desconhecido: $FeatureName. Rode sem argumentos para ver a lista." }
    if (-not (Test-Path $Key)) { New-Item $Key | Out-Null }

    if ($FeatureName -eq 'FundoTema') { New-ItemProperty $Key $FeatureName -Value $NewValue -PropertyType String -Force | Out-Null }
    else { New-ItemProperty $Key $FeatureName -Value ([int]$NewValue) -PropertyType DWord -Force | Out-Null }
    "$FeatureName = $NewValue"

    if ($Reload) { $feature.Parts -split ' ' | ForEach-Object { & "$PSScriptRoot\reload-$_.ps1" } }
    else { "Vale depois de recarregar: $(($feature.Parts -split ' ' | ForEach-Object { "tools\reload-$_.ps1" }) -join ', ')" }
}

switch ($Action) {
    'list' { Show-Features }
    'on' { Set-Feature $Name 1 }
    'off' { Set-Feature $Name 0 }
    'set' { Set-Feature $Name $Value }
}
