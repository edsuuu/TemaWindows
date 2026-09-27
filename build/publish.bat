@rem Uso: publish.bat <binário compilado>. Copia para bin\. O binário antigo pode estar carregado (Explorer, Iniciar,
@rem Configurações, papel de parede): renomear funciona mesmo assim, e os .old somem no próximo build em que estiverem soltos.
del /q "%BIN%\%~n1.*.old" 2>nul
if exist "%BIN%\%~nx1" ren "%BIN%\%~nx1" "%~n1.%RANDOM%.old" || exit /b 1
copy /y "%~1" "%BIN%\" >nul || exit /b 1
exit /b 0
