# Runs the whole chain on THIS computer, no Raspberry Pi and no ESP32 needed:
#   fake ESP32 (scripts/fake-esp32.cjs) -> MQTT broker (scripts/dev-broker.cjs) -> Node-RED -> dashboard
# Parameters: -Speed 1..4 (1 = real 2 s per floor). Re-run is safe: running parts are skipped.
param([int]$Speed = 1)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$logs = Join-Path $root 'windows\logs'
New-Item -ItemType Directory -Force $logs | Out-Null
$node = (Get-Command node.exe -ErrorAction Stop).Source

if (-not (Test-Path (Join-Path $root 'windows\node-red\node_modules'))) { & npm.cmd ci --prefix (Join-Path $root 'windows\node-red') }
if (-not (Test-Path (Join-Path $root 'windows\local-sim\node_modules'))) { & npm.cmd install --prefix (Join-Path $root 'windows\local-sim') }
& $node (Join-Path $root 'scripts\make-local-secrets.cjs')
& $node (Join-Path $root 'scripts\generate-flows.cjs')

function Start-Hidden($name, $argList, $env) {
    foreach ($k in $env.Keys) { Set-Item -Path "Env:$k" -Value $env[$k] }
    Start-Process -FilePath $node -ArgumentList $argList -WorkingDirectory $root -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $logs "$name.log") -RedirectStandardError (Join-Path $logs "$name-error.log") | Out-Null
}
function Listening($port) { [bool](Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue) }

if (-not (Listening 1883)) { Start-Hidden 'dev-broker' @(('"' + (Join-Path $root 'scripts\dev-broker.cjs') + '"')) @{}; Start-Sleep -Seconds 1 }
if (-not (Listening 1880)) {
    $red = Join-Path $root 'windows\node-red'
    Start-Hidden 'node-red' @(('"' + (Join-Path $red 'node_modules\node-red\red.js') + '"'), '--userDir', ('"' + $red + '"'), '--settings', ('"' + (Join-Path $red 'settings.js') + '"')) @{}
}
$fake = Get-CimInstance Win32_Process -Filter "Name='node.exe'" | Where-Object { $_.CommandLine -like '*fake-esp32.cjs*' }
if (-not $fake) { Start-Hidden 'fake-esp32' @(('"' + (Join-Path $root 'scripts\fake-esp32.cjs') + '"')) @{ SPEED = "$Speed" } }

Write-Host 'Dashboard: http://127.0.0.1:1880/dashboard/elevators'
Write-Host 'Logs: windows\logs (dev-broker, node-red, fake-esp32)'
Write-Host 'Stop: scripts\stop-local-sim.ps1'
