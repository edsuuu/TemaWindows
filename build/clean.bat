@rem Apaga os intermediários do build atual (build\obj\<projeto>) e a pasta build\obj se ficar vazia.
if exist "%OBJ%" rmdir /s /q "%OBJ%"
rmdir "%ROOT%\build\obj" 2>nul
exit /b 0
