$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskLogs = Join-Path $taskRoot 'windows\logs'
New-Item -ItemType Directory -Force $taskLogs | Out-Null
$taskSecrets = Get-Content (Join-Path $taskRoot 'windows\local-secrets.json') -Raw | ConvertFrom-Json
if ($taskSecrets.networkMode -eq 'windows-hotspot') {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'enable-hotspot.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Windows hotspot could not start. See the message above.' }
}
$taskBroker = Join-Path $taskRoot 'windows\mosquitto\mosquitto.exe'
$taskNode = (Get-Command node.exe -ErrorAction Stop).Source
if (-not (Get-NetTCPConnection -State Listen -LocalPort 1883 -ErrorAction SilentlyContinue)) {
    Start-Process -FilePath $taskBroker -ArgumentList @('-c', ('"' + (Join-Path $taskRoot 'windows\broker\mosquitto.conf') + '"')) -WorkingDirectory (Split-Path $taskBroker) -WindowStyle Hidden -RedirectStandardOutput (Join-Path $taskLogs 'mosquitto.log') -RedirectStandardError (Join-Path $taskLogs 'mosquitto-error.log') | Out-Null
}
if (-not (Get-NetTCPConnection -State Listen -LocalPort 1880 -ErrorAction SilentlyContinue)) {
    $taskRed = Join-Path $taskRoot 'windows\node-red'
    $taskArgs = @(('"' + (Join-Path $taskRed 'node_modules\node-red\red.js') + '"'), '--userDir', ('"' + $taskRed + '"'), '--settings', ('"' + (Join-Path $taskRed 'settings.js') + '"'))
    Start-Process -FilePath $taskNode -ArgumentList $taskArgs -WorkingDirectory $taskRoot -WindowStyle Hidden -RedirectStandardOutput (Join-Path $taskLogs 'node-red.log') -RedirectStandardError (Join-Path $taskLogs 'node-red-error.log') | Out-Null
}
Write-Host 'Dashboard: http://127.0.0.1:1880/dashboard/elevators'
Write-Host 'Node-RED editor: http://127.0.0.1:1880'
