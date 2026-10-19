@echo off
rem Sampling profiler (RT_SAMPLE): when you close the window it writes log-win-sample.txt, showing where the time goes (normal vs slow frames).
cd /d "%~dp0"
set RT_FPS=1
set RT_SAMPLE=1
build-win\castlevania.exe ..\orig\game.dol > log-win-sample.txt 2>&1
