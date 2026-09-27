@echo off
rem Ferramentas de desenvolvimento -> tools\: capture.exe, capture-wallpaper.exe e effects-test.exe
setlocal
call "%~dp0env.bat" || exit /b 1
set "OBJ=%ROOT%\build\obj\tools"
set "TOOLS=%ROOT%\tools"
if not exist "%OBJ%" mkdir "%OBJ%"

cl %CFLAGS% /Fo"%OBJ%/" "%TOOLS%\capture.cpp" /link /OUT:"%TOOLS%\capture.exe" d3d11.lib dxgi.lib windowscodecs.lib ole32.lib user32.lib || exit /b 1
cl %CFLAGS% /Fo"%OBJ%/" "%TOOLS%\capture-wallpaper.cpp" /link /OUT:"%TOOLS%\capture-wallpaper.exe" user32.lib gdi32.lib windowscodecs.lib ole32.lib || exit /b 1
cl %CFLAGS% /Fo"%OBJ%/" "%TOOLS%\effects-test.cpp" "%SRC%\common\glass.cpp" "%SRC%\common\log.cpp" "%SRC%\common\paths.cpp" "%SRC%\common\registry.cpp" ^
   /link /OUT:"%TOOLS%\effects-test.exe" windowsapp.lib ole32.lib user32.lib advapi32.lib || exit /b 1

call "%~dp0clean.bat"
