@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\start-mockup.ps1"
start "" "http://127.0.0.1:1880/dashboard/elevators"
