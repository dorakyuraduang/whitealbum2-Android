@echo off
if not exist "%~dp0logs" mkdir "%~dp0logs"
"%~dp0out\WA2_SDL2_x64.exe" --root "%~dp0res" --report "%~dp0logs\runtime.json" %* > "%~dp0logs\launch-console.log" 2>&1
set "wa2_exit=%errorlevel%"
if "%wa2_exit%"=="0" exit /b 0
if /i "%~1"=="--no-pause" exit /b %wa2_exit%
echo WA2 x64 stopped. See logs\launch-console.log and logs\runtime.json.
echo.
pause
exit /b %wa2_exit%
