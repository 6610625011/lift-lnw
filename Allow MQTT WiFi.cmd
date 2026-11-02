@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\request-firewall.ps1"
echo After accepting Yes, the ESP32 reconnects automatically.
pause
