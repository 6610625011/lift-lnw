# Use Windows PowerShell 5.1 for WinRT support. Starts a private 2.4 GHz link.
param([switch]$Restart)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskSecretsPath = Join-Path $taskRoot 'windows\local-secrets.json'
$taskSecrets = Get-Content $taskSecretsPath -Raw | ConvertFrom-Json
$taskProfile = [Windows.Networking.Connectivity.NetworkInformation,Windows.Networking.Connectivity,ContentType=WindowsRuntime]::GetConnectionProfiles() | Where-Object { $_.IsWlanConnectionProfile } | Select-Object -First 1
if (-not $taskProfile) { throw 'Connect Windows to Wi-Fi before starting the hotspot.' }
$taskManager = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager,Windows.Networking.NetworkOperators,ContentType=WindowsRuntime]::CreateFromConnectionProfile($taskProfile)
$taskAsOperation = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.IsGenericMethod -and $_.GetGenericArguments().Count -eq 1 -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } | Select-Object -First 1
$taskResultType=[Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult,Windows.Networking.NetworkOperators,ContentType=WindowsRuntime]
if ($taskManager.TetheringOperationalState.ToString() -eq 'On') {
    if (-not $Restart) { Write-Host 'Windows hotspot is already on.'; exit 0 }
    $taskStop=$taskAsOperation.MakeGenericMethod($taskResultType).Invoke($null,@($taskManager.StopTetheringAsync()))
    if (-not $taskStop.Wait(20000) -or $taskStop.Result.Status.ToString() -ne 'Success') { throw 'Could not restart the existing hotspot.' }
}
$taskConfig = $taskManager.GetCurrentAccessPointConfiguration()
if (-not $taskSecrets.hotspotPrevious) {
    $taskSecrets | Add-Member -NotePropertyName hotspotPrevious -NotePropertyValue @{ssid=$taskConfig.Ssid;password=$taskConfig.Passphrase;band=$taskConfig.Band.ToString()}
}
if (-not $taskSecrets.hotspotSsid) {
    $taskSecrets | Add-Member -NotePropertyName hotspotSsid -NotePropertyValue 'Elevator-Mockup'
    $taskBytes=New-Object byte[] 16
    [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($taskBytes)
    $taskSecrets | Add-Member -NotePropertyName hotspotPassword -NotePropertyValue ([BitConverter]::ToString($taskBytes).Replace('-',''))
}
[IO.File]::WriteAllText($taskSecretsPath,($taskSecrets | ConvertTo-Json -Depth 5),(New-Object Text.UTF8Encoding($false)))
$taskConfig.Ssid=$taskSecrets.hotspotSsid
$taskConfig.Passphrase=$taskSecrets.hotspotPassword
$taskBand=[Windows.Networking.NetworkOperators.TetheringWiFiBand]::TwoPointFourGigahertz
if (-not $taskConfig.IsBandSupported($taskBand)) { throw 'This adapter cannot create a 2.4 GHz hotspot.' }
$taskConfig.Band=$taskBand
$taskAsAction = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and -not $_.IsGenericMethod -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.FullName -eq 'Windows.Foundation.IAsyncAction' } | Select-Object -First 1
$taskConfigure=$taskAsAction.Invoke($null,@($taskManager.ConfigureAccessPointAsync($taskConfig)))
if (-not $taskConfigure.Wait(20000)) { throw 'Hotspot configuration timed out.' }
$taskSessionType=[Windows.Networking.NetworkOperators.NetworkOperatorTetheringSessionAccessPointConfiguration,Windows.Networking.NetworkOperators,ContentType=WindowsRuntime]
$taskSession=New-Object $taskSessionType
$taskSession.Ssid=$taskConfig.Ssid
$taskSession.Passphrase=$taskConfig.Passphrase
$taskSession.Band=$taskBand
$taskSession.AuthenticationKind=[Windows.Networking.NetworkOperators.TetheringWiFiAuthenticationKind]::Wpa2
# Ask Windows to favor the 2.4 GHz hotspot on this single-radio adapter.
$taskSession.PerformancePriority=[Windows.Networking.NetworkOperators.TetheringWiFiPerformancePriority]::TetheringOverStation
$taskStart=$taskAsOperation.MakeGenericMethod($taskResultType).Invoke($null,@($taskManager.StartTetheringAsync($taskSession)))
if (-not $taskStart.Wait(20000)) { throw 'Hotspot start timed out.' }
if ($taskStart.Result.Status.ToString() -ne 'Success') { throw ('Hotspot could not start: '+$taskStart.Result.Status+' '+$taskStart.Result.AdditionalErrorMessage) }
Write-Host ('Windows hotspot started: '+$taskSecrets.hotspotSsid+' (2.4 GHz).')
