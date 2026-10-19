@echo off
rem Igual que play-windows.bat pero con el contador de rendimiento (RT_FPS) y la salida guardada en log-win.txt (carpeta runtime).
cd /d "%~dp0"
set RT_FPS=1
build-win\castlevania.exe ..\orig\game.dol > log-win.txt 2>&1
