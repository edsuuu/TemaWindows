@echo off
rem Compila tudo: as três DLLs, o FundoVivo e o FundoConfig em bin\, e as ferramentas em tools\. Não recarrega nada:
rem para trocar o que está rodando, use os scripts tools\reload-*.ps1 (e rode tools\effects-test.exe antes de recarregar
rem a barra).
setlocal
for %%p in (taskbar menus settings wallpaper contextmenu tools) do (
    echo == %%p
    call "%~dp0build-%%p.bat" || exit /b 1
)
echo ok
