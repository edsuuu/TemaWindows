@echo off
rem FundoVivo.exe (papel de parede animado com widgets) -> bin\
setlocal
call "%~dp0env.bat" || exit /b 1
set "OBJ=%ROOT%\build\obj\wallpaper"
if not exist "%OBJ%" mkdir "%OBJ%"

cl %CFLAGS% /Fo"%OBJ%/" "%SRC%\wallpaper\*.cpp" "%SRC%\common\registry.cpp" "%SRC%\common\paths.cpp" ^
   /link /SUBSYSTEM:WINDOWS /OUT:"%OBJ%\FundoVivo.exe" ^
   d3d11.lib dxgi.lib d3dcompiler.lib d2d1.lib dwrite.lib windowscodecs.lib ole32.lib user32.lib shell32.lib dwmapi.lib ^
   wtsapi32.lib advapi32.lib gdi32.lib windowsapp.lib shcore.lib dxguid.lib || exit /b 1

call "%~dp0publish.bat" "%OBJ%\FundoVivo.exe" || exit /b 1
call "%~dp0clean.bat"
