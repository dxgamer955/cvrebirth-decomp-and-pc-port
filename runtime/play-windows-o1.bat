@echo off
rem Version compilada con -O1 (build-win-o1), con prioridad normal. Log: log-win-o1.txt
cd /d "%~dp0"
set RT_FPS=1
set RT_NO_PRIORITY=1
build-win-o1\castlevania.exe ..\orig\game.dol > log-win-o1.txt 2>&1
