@rem Ambiente comum dos builds: compilador x64 do VS 2022 Build Tools, pastas do projeto e opções do cl.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
for %%i in ("%~dp0..") do set "ROOT=%%~fi"
set "SRC=%ROOT%\src"
set "BIN=%ROOT%\bin"
set CFLAGS=/nologo /utf-8 /std:c++20 /EHsc /O2 /MT /W3 /MP /DNOMINMAX /I"%ROOT%\src"
if not exist "%BIN%" mkdir "%BIN%"
if not exist "%ROOT%\logs" mkdir "%ROOT%\logs"
exit /b 0
