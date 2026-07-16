@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
set "COMMAND=%~1"
if "%COMMAND%"=="" set "COMMAND=build"
if not "%~1"=="" shift
py -3 "%SCRIPT_DIR%%COMMAND%.py" %*
exit /b %ERRORLEVEL%
