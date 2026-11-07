@echo off
title Editor Collab Server
cd /d "%~dp0"
where py >nul 2>nul && (py -3 collab_server.py & goto done)
where python >nul 2>nul && (python collab_server.py & goto done)
echo.
echo   Python isn't installed.
echo   Get it from https://www.python.org/downloads/  (tick "Add python.exe to PATH")
echo   then double-click this file again.
echo.
:done
pause
