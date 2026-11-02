# Match the upstream Wi-Fi band to the ESP32 hotspot on this single-radio laptop.
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskSecretsPath=Join-Path $taskRoot 'windows\local-secrets.json'
try {
    $taskCurrent=Get-NetAdapterAdvancedProperty -Name 'Wi-Fi' -RegistryKeyword 'PreferredBand'
    $taskSecrets=Get-Content $taskSecretsPath -Raw | ConvertFrom-Json
    if (-not $taskSecrets.wifiAdapterPreviousBand) {
        $taskSecrets | Add-Member -NotePropertyName wifiAdapterPreviousBand -NotePropertyValue ([string]$taskCurrent.RegistryValue[0])
        [IO.File]::WriteAllText($taskSecretsPath,($taskSecrets | ConvertTo-Json -Depth 5),(New-Object Text.UTF8Encoding($false)))
    }
    Set-NetAdapterAdvancedProperty -Name 'Wi-Fi' -RegistryKeyword 'PreferredBand' -RegistryValue '1'
    'Wi-Fi now prefers 2.4 GHz. Previous adapter preference was saved.' | Set-Content (Join-Path $taskRoot 'windows\logs\wifi-band-result.txt')
} catch {
    $_.Exception.Message | Set-Content (Join-Path $taskRoot 'windows\logs\wifi-band-result.txt')
    exit 1
}
