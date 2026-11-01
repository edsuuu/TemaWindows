@echo off
rem Instalador do Wallget -> bin\Wallget-Setup.exe: compila o FundoVivo e o FundoConfig e empacota com o Inno Setup
rem (winget install JRSoftware.InnoSetup --scope user).
setlocal
call "%~dp0build-wallpaper.bat" || exit /b 1

set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
"%ISCC%" /Q "%~dp0installer.iss" || exit /b 1
echo ok
