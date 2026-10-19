@echo off
rem Builds the native PC port (castlevania.exe) end to end from YOUR copy of the game.
rem
rem   build.bat "C:\path\game.wad"          (you can also drag the WAD onto this file)
rem   build.bat                              looks for a .wad next to this file (or in orig\), or uses orig\game.dol if it exists
rem   build.bat "game.wad" --install-deps    install what is missing with pacman in MSYS2
rem   build.bat "game.wad" --run             run the game when the build finishes
rem
rem Requires MSYS2 (https://www.msys2.org). This file only locates it and calls build.sh inside its UCRT64 shell.
setlocal
cd /d "%~dp0"

set "MSYS2="
if defined MSYS2_ROOT if exist "%MSYS2_ROOT%\usr\bin\bash.exe" set "MSYS2=%MSYS2_ROOT%"
if not defined MSYS2 for %%D in (C D E F) do if exist "%%D:\msys64\usr\bin\bash.exe" if not defined MSYS2 set "MSYS2=%%D:\msys64"
if not defined MSYS2 (
  echo MSYS2 was not found. Install it from https://www.msys2.org and run this file again.
  echo If it is installed somewhere else, set the MSYS2_ROOT variable to that folder.
  pause
  exit /b 1
)

rem The first argument (if it does not start with -) is the WAD or the content folder; the rest are build.sh options
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
  echo The build failed. Check the messages above.
  pause
  exit /b 1
)
echo.
echo Done. Run runtime\play-windows.bat
pause
