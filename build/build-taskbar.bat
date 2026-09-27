@echo off
rem TemaBarra.dll (barra de tarefas, dentro do Explorer) -> bin\
setlocal
call "%~dp0env.bat" || exit /b 1
set "OBJ=%ROOT%\build\obj\taskbar"
if not exist "%OBJ%" mkdir "%OBJ%"

cl %CFLAGS% /LD /Fo"%OBJ%/" "%SRC%\taskbar\*.cpp" "%SRC%\common\*.cpp" ^
   /link /OUT:"%OBJ%\TemaBarra.dll" /IMPLIB:"%OBJ%\TemaBarra.lib" ^
   windowsapp.lib ole32.lib user32.lib advapi32.lib dwmapi.lib || exit /b 1

call "%~dp0publish.bat" "%OBJ%\TemaBarra.dll" || exit /b 1
call "%~dp0clean.bat"
