@echo off
rem Compila el port nativo a PC (castlevania.exe) de principio a fin a partir de TU copia del juego.
rem
rem   build.bat "C:\ruta\juego.wad"        (tambien puedes arrastrar el WAD sobre este archivo)
rem   build.bat                             si orig\game.dol y orig\files ya existen
rem   build.bat "juego.wad" --install-deps  instala con pacman lo que falte en MSYS2
rem   build.bat "juego.wad" --run           ejecuta el juego al terminar
rem
rem Hace falta MSYS2 (https://www.msys2.org). Este archivo solo lo localiza y llama a build.sh dentro de su terminal UCRT64.
setlocal
cd /d "%~dp0"

set "MSYS2="
if defined MSYS2_ROOT if exist "%MSYS2_ROOT%\usr\bin\bash.exe" set "MSYS2=%MSYS2_ROOT%"
if not defined MSYS2 for %%D in (C D E F) do if exist "%%D:\msys64\usr\bin\bash.exe" if not defined MSYS2 set "MSYS2=%%D:\msys64"
if not defined MSYS2 (
  echo No se encontro MSYS2. Instalalo desde https://www.msys2.org y vuelve a ejecutar este archivo.
  echo Si lo instalaste en otra carpeta, define la variable MSYS2_ROOT con esa ruta.
  pause
  exit /b 1
)

rem El primer argumento (si no empieza por -) es el WAD o la carpeta de contenido; el resto son opciones de build.sh
set "RT_WAD="
set "OPTS="
:args
if "%~1"=="" goto run
set "A=%~1"
if "%A:~0,1%"=="-" (set "OPTS=%OPTS% %~1") else (set "RT_WAD=%~f1")
shift
goto args

:run
set MSYSTEM=UCRT64
set CHERE_INVOKING=1
"%MSYS2%\usr\bin\bash.exe" -lc "./build.sh%OPTS%"
if errorlevel 1 (
  echo.
  echo La compilacion fallo. Revisa los mensajes de arriba.
  pause
  exit /b 1
)
echo.
echo Listo. Ejecuta runtime\play-windows.bat
pause
