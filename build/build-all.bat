@echo off
rem Compila tudo: as três DLLs e o FundoVivo em bin\, e as ferramentas em tools\. Não recarrega nada: para trocar o que
rem está rodando, use os scripts tools\reload-*.ps1 (e rode tools\effects-test.exe antes de recarregar a barra).
setlocal
for %%p in (taskbar menus settings wallpaper tools) do (
    echo == %%p
    call "%~dp0build-%%p.bat" || exit /b 1
)
echo ok
