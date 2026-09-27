@echo off
rem TemaJanelas.dll (vidro nas Configurações) -> bin\
setlocal
call "%~dp0env.bat" || exit /b 1
set "OBJ=%ROOT%\build\obj\settings"
if not exist "%OBJ%" mkdir "%OBJ%"

cl %CFLAGS% /LD /Fo"%OBJ%/" "%SRC%\settings\*.cpp" "%SRC%\common\*.cpp" ^
   /link /OUT:"%OBJ%\TemaJanelas.dll" /IMPLIB:"%OBJ%\TemaJanelas.lib" ^
   windowsapp.lib ole32.lib user32.lib advapi32.lib || exit /b 1

call "%~dp0publish.bat" "%OBJ%\TemaJanelas.dll" || exit /b 1
call "%~dp0appcontainer-access.bat" TemaJanelas || exit /b 1
call "%~dp0clean.bat"
