@echo off
rem TemaMenus.dll (Iniciar, pesquisa, Configurações Rápidas e notificações) -> bin\
setlocal
call "%~dp0env.bat" || exit /b 1
set "OBJ=%ROOT%\build\obj\menus"
if not exist "%OBJ%" mkdir "%OBJ%"

cl %CFLAGS% /LD /Fo"%OBJ%/" "%SRC%\menus\*.cpp" "%SRC%\common\*.cpp" ^
   /link /OUT:"%OBJ%\TemaMenus.dll" /IMPLIB:"%OBJ%\TemaMenus.lib" ^
   windowsapp.lib ole32.lib user32.lib advapi32.lib shell32.lib || exit /b 1

call "%~dp0publish.bat" "%OBJ%\TemaMenus.dll" || exit /b 1
call "%~dp0appcontainer-access.bat" TemaMenus || exit /b 1
call "%~dp0clean.bat"
