@echo off
rem Perfilador de muestreo (RT_SAMPLE): al cerrar la ventana escribe en log-win-sample.txt donde se pierde el tiempo (normal vs fotogramas lentos).
cd /d "%~dp0"
set RT_FPS=1
set RT_SAMPLE=1
build-win\castlevania.exe ..\orig\game.dol > log-win-sample.txt 2>&1
