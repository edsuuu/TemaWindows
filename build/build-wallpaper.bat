@echo off
rem FundoVivo.exe (papel de parede animado com widgets) e FundoConfig.exe (cores, JSON e cidade dos widgets, com prévia
rem feita pelo mesmo código: linka os mesmos .obj, menos o main do FundoVivo) -> bin\
setlocal
call "%~dp0env.bat" || exit /b 1
set "OBJ=%ROOT%\build\obj\wallpaper"
set LIBS=d3d11.lib dxgi.lib d3dcompiler.lib d2d1.lib dwrite.lib windowscodecs.lib ole32.lib user32.lib shell32.lib dwmapi.lib ^
   wtsapi32.lib advapi32.lib gdi32.lib windowsapp.lib shcore.lib dxguid.lib
if not exist "%OBJ%\config" mkdir "%OBJ%\config"

cl %CFLAGS% /c /Fo"%OBJ%/" "%SRC%\wallpaper\*.cpp" "%SRC%\common\registry.cpp" "%SRC%\common\paths.cpp" || exit /b 1
link /nologo "%OBJ%\*.obj" /SUBSYSTEM:WINDOWS /OUT:"%OBJ%\FundoVivo.exe" /MANIFEST:EMBED /MANIFESTINPUT:"%SRC%\wallpaper\FundoVivo.manifest" ^
   %LIBS% || exit /b 1

del "%OBJ%\main.obj"
cl %CFLAGS% /c /Fo"%OBJ%\config/" "%SRC%\config\main.cpp" || exit /b 1
link /nologo "%OBJ%\*.obj" "%OBJ%\config\main.obj" /SUBSYSTEM:WINDOWS /OUT:"%OBJ%\FundoConfig.exe" /MANIFEST:EMBED ^
   /MANIFESTDEPENDENCY:"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'" ^
   %LIBS% comdlg32.lib uxtheme.lib || exit /b 1

call "%~dp0publish.bat" "%OBJ%\FundoVivo.exe" || exit /b 1
call "%~dp0publish.bat" "%OBJ%\FundoConfig.exe" || exit /b 1
call "%~dp0clean.bat"
