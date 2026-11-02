@echo off
rem Whole elevator chain on this PC (no Raspberry Pi, no ESP32): emulator -> MQTT -> Node-RED -> dashboard.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\start-local-sim.ps1" %*
start "" http://127.0.0.1:1880/dashboard/elevators
pause
