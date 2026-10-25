@echo off
rem TemaContexto.dll (itens do "Mostrar mais opções" e Abrir WSL no menu novo do clique direito) -> bin\. Os pacotes
rem que ligam a DLL ao menu são gerados e registrados por tools\reload-contextmenu.ps1.
setlocal
call "%~dp0env.bat" || exit /b 1
set "OBJ=%ROOT%\build\obj\contextmenu"
if not exist "%OBJ%" mkdir "%OBJ%"

cl %CFLAGS% /LD /Fo"%OBJ%/" "%SRC%\contextmenu\*.cpp" "%SRC%\common\log.cpp" "%SRC%\common\paths.cpp" ^
   /link /OUT:"%OBJ%\TemaContexto.dll" /IMPLIB:"%OBJ%\TemaContexto.lib" ^
   windowsapp.lib ole32.lib oleaut32.lib user32.lib gdi32.lib advapi32.lib shell32.lib shlwapi.lib version.lib || exit /b 1

call "%~dp0publish.bat" "%OBJ%\TemaContexto.dll" || exit /b 1
call "%~dp0clean.bat"
