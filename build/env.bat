@rem Ambiente comum dos builds: compilador x64 do Visual Studio achado pelo vswhere (aqui o Build Tools, no CI o do
@rem runner), pastas do projeto e opções do cl.
set "VSDIR="
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR echo Falta o Visual Studio com C++ (Build Tools: Desenvolvimento para desktop com C++) & exit /b 1
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
for %%i in ("%~dp0..") do set "ROOT=%%~fi"
set "SRC=%ROOT%\src"
set "BIN=%ROOT%\bin"
set CFLAGS=/nologo /utf-8 /std:c++20 /EHsc /O2 /MT /W3 /MP /DNOMINMAX /I"%ROOT%\src"
if not exist "%BIN%" mkdir "%BIN%"
if not exist "%ROOT%\logs" mkdir "%ROOT%\logs"
exit /b 0
