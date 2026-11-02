$ErrorActionPreference = 'Stop'
$taskScript = Join-Path $PSScriptRoot 'allow-mqtt-firewall.ps1'
Start-Process -FilePath powershell.exe -Verb RunAs -WindowStyle Hidden -Wait -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$taskScript+'"'))
$taskResult = Join-Path (Split-Path -Parent $PSScriptRoot) 'windows\logs\firewall-result.txt'
if (Test-Path $taskResult) { Get-Content $taskResult }
