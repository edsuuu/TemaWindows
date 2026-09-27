# Recarrega o TemaJanelas (vidro nas Configurações): recompila se pedido e troca o vigia. As Configurações já abertas
# continuam com a DLL velha até serem fechadas e abertas de novo.
# Uso: .\reload-settings.ps1 [-Build]
param([switch]$Build)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

Assert-NoGame
if ($Build) { Invoke-Build settings }
Stop-Injector TemaJanelas.dll
Start-Injector TemaJanelas.dll
