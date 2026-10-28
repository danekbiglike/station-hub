@echo off
setlocal
set /p "STATION_HOST=Station IPv4 address: "
if "%STATION_HOST%"=="" exit /b 1
echo Enter the pairing key shown on your TV.
python -u "%~dp0station_audio.py" "%STATION_HOST%"
echo Transmission stopped.
pause
