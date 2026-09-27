# Recarrega o FundoVivo: recompila se pedido, para o que estiver rodando (de bin\ ou de um caminho antigo) e inicia o de
# bin\. Se pedido, tira um print da janela dele inteira (todos os monitores).
# Uso: .\reload-wallpaper.ps1 [-Build] [-Shot <saida.png>]
param([switch]$Build, [string]$Shot)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

Assert-NoGame
if ($Build) { Invoke-Build wallpaper }
Get-Process FundoVivo -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 1
Start-Process "$Bin\FundoVivo.exe"
if ($Shot) {
    Start-Sleep 4
    & "$PSScriptRoot\capture-wallpaper.exe" $Shot
}
