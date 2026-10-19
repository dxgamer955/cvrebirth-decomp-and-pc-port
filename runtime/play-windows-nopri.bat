@echo off
rem Igual que play-windows-debug.bat pero SIN subir la prioridad del proceso (RT_NO_PRIORITY). Log: log-win-nopri.txt
cd /d "%~dp0"
set RT_FPS=1
set RT_NO_PRIORITY=1
build-win\castlevania.exe ..\orig\game.dol > log-win-nopri.txt 2>&1
