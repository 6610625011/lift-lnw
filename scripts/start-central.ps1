$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$logs = Join-Path $root 'windows\logs'
New-Item -ItemType Directory -Force $logs | Out-Null
$node = (Get-Command node.exe -ErrorAction Stop).Source
$python = (Get-Command python.exe -ErrorAction Stop).Source

if (-not (Test-Path (Join-Path $root 'windows\node-red\node_modules'))) { & npm.cmd ci --prefix (Join-Path $root 'windows\node-red') }
if (-not (Test-Path (Join-Path $root 'windows\local-sim\node_modules'))) { & npm.cmd install --prefix (Join-Path $root 'windows\local-sim') }
& $node (Join-Path $root 'scripts\make-local-secrets.cjs')
& $node (Join-Path $root 'scripts\generate-flows.cjs')

function Listening($port) { [bool](Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue) }

# 1. Start dev MQTT Broker bound to 0.0.0.0 so Raspberry Pi and ESP32 can connect
if (-not (Listening 1883)) {
    Write-Host "[1/3] Starting MQTT Broker on port 1883 (0.0.0.0)..." -ForegroundColor Cyan
    $env:MQTT_BIND = '0.0.0.0'
    Start-Process -FilePath $node -ArgumentList @(('"' + (Join-Path $root 'scripts\dev-broker.cjs') + '"')) -WorkingDirectory $root -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $logs "dev-broker.log") -RedirectStandardError (Join-Path $logs "dev-broker-error.log") | Out-Null
    Start-Sleep -Seconds 1
}

# 2. Start Node-RED Dashboard
if (-not (Listening 1880)) {
    Write-Host "[2/3] Starting Node-RED Dashboard on port 1880..." -ForegroundColor Cyan
    $red = Join-Path $root 'windows\node-red'
    Start-Process -FilePath $node -ArgumentList @(('"' + (Join-Path $red 'node_modules\node-red\red.js') + '"'), '--userDir', ('"' + $red + '"'), '--settings', ('"' + (Join-Path $red 'settings.js') + '"')) -WorkingDirectory $root -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $logs "node-red.log") -RedirectStandardError (Join-Path $logs "node-red-error.log") | Out-Null
}

# Find local IPv4 address
$ip = (Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.InterfaceAlias -notlike '*Loopback*' -and $_.IPAddress -notlike '169.254*' } | Select-Object -First 1).IPAddress

Write-Host "==========================================================" -ForegroundColor Green
Write-Host " Central Control Unit (Windows) Ready!" -ForegroundColor Green
Write-Host " Windows IP Address : $ip" -ForegroundColor Yellow
Write-Host " Dashboard URL      : http://127.0.0.1:1880/dashboard/elevators" -ForegroundColor Yellow
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "Starting Python Central Controller (Cost Function & Card DB)..." -ForegroundColor Cyan

# 3. Launch Central Controller in foreground
& $python -u (Join-Path $root 'windows\central_controller.py')

