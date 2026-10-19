@echo off
rem Runs the native runtime on Windows (build it first with ..\build.bat).
rem Uses orig\game.dol and the extracted game files in orig\files.
cd /d "%~dp0"
build-win\castlevania.exe ..\orig\game.dol
pause
