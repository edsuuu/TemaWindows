@echo off
rem TemaBarra.dll (barra de tarefas, dentro do Explorer) -> bin\. O player do Spotify usa a leitura da música e o
rem equalizador do papel de parede (media, audio e timing), as cores dele (palette) e a mesma conta de tela cheia
rem (fullscreen).
setlocal
call "%~dp0env.bat" || exit /b 1
set "OBJ=%ROOT%\build\obj\taskbar"
if not exist "%OBJ%" mkdir "%OBJ%"

cl %CFLAGS% /LD /Fo"%OBJ%/" "%SRC%\taskbar\*.cpp" "%SRC%\common\*.cpp" ^
   "%SRC%\wallpaper\media.cpp" "%SRC%\wallpaper\audio.cpp" "%SRC%\wallpaper\timing.cpp" "%SRC%\wallpaper\palette.cpp" ^
   "%SRC%\wallpaper\fullscreen.cpp" ^
   /link /OUT:"%OBJ%\TemaBarra.dll" /IMPLIB:"%OBJ%\TemaBarra.lib" ^
   windowsapp.lib ole32.lib user32.lib advapi32.lib dwmapi.lib shell32.lib shcore.lib windowscodecs.lib || exit /b 1

call "%~dp0publish.bat" "%OBJ%\TemaBarra.dll" || exit /b 1
call "%~dp0clean.bat"
