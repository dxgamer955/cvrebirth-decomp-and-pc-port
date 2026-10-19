@echo off
rem Lanza el runtime nativo en Windows (hace falta haber compilado: ver README del runtime / PROGRESS.md).
rem Usa orig\game.dol y los ficheros del juego extraidos que ya tienes en el proyecto.
cd /d "%~dp0"
build-win\castlevania.exe ..\orig\game.dol
pause
