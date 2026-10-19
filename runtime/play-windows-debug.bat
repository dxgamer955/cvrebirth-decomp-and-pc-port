@echo off
rem Same as play-windows.bat but with the performance counters (RT_FPS); the output is saved to log-win.txt (runtime folder).
cd /d "%~dp0"
set RT_FPS=1
build-win\castlevania.exe ..\orig\game.dol > log-win.txt 2>&1
