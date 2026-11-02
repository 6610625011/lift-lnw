# Optional: run as Administrator to restore the adapter preference saved at setup.
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskSecrets=Get-Content (Join-Path $taskRoot 'windows\local-secrets.json') -Raw | ConvertFrom-Json
if ($null -eq $taskSecrets.wifiAdapterPreviousBand) { throw 'No saved adapter preference exists.' }
Set-NetAdapterAdvancedProperty -Name 'Wi-Fi' -RegistryKeyword 'PreferredBand' -RegistryValue ([string]$taskSecrets.wifiAdapterPreviousBand)
Write-Host 'Restored the original Wi-Fi band preference.'
