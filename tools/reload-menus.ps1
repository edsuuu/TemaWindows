# Recarrega o TemaMenus: para o vigia, derruba os hosts do Iniciar, da pesquisa e dos painéis (o Windows os relança),
# recompila se pedido e inicia o vigia de bin\. Não mexe no explorer.exe.
# Uso: .\reload-menus.ps1 [-Build]
param([switch]$Build)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

Assert-NoGame
Stop-Injector TemaMenus.dll
Stop-Process -Name StartMenuExperienceHost, SearchHost, ShellExperienceHost, ShellHost -Force -ErrorAction SilentlyContinue
Start-Sleep 1
if ($Build) { Invoke-Build menus }
Start-Injector TemaMenus.dll
