@echo off
setlocal

where py >nul 2>nul
if not errorlevel 1 goto use_py

where python >nul 2>nul
if not errorlevel 1 goto use_python

where python3 >nul 2>nul
if not errorlevel 1 goto use_python3

echo ERROR: Python 3 is required to run the RP86 build driver. 1>&2
exit /b 1

:use_py
py -3 "%~dp0build.py" %*
exit /b %errorlevel%

:use_python
python "%~dp0build.py" %*
exit /b %errorlevel%

:use_python3
python3 "%~dp0build.py" %*
exit /b %errorlevel%
