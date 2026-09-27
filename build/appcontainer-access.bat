@rem Uso: appcontainer-access.bat <nome>. Os hosts UWP (AppContainer) precisam ler bin\<nome>.dll e escrever em
@rem logs\<nome>.log: permissão para "todos os pacotes de aplicativo" (S-1-15-2-1) e os restritos (S-1-15-2-2).
icacls "%BIN%\%~1.dll" /grant "*S-1-15-2-1:(RX)" "*S-1-15-2-2:(RX)" >nul || exit /b 1
if not exist "%ROOT%\logs\%~1.log" type nul > "%ROOT%\logs\%~1.log"
icacls "%ROOT%\logs" /grant "*S-1-15-2-1:(RX)" "*S-1-15-2-2:(RX)" >nul || exit /b 1
icacls "%ROOT%\logs\%~1.log" /grant "*S-1-15-2-1:(M)" "*S-1-15-2-2:(M)" >nul || exit /b 1
exit /b 0
