# Run elevated once. Only Mosquitto on Wi-Fi, TCP 1883, local subnet is allowed.
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskProgram = Join-Path $taskRoot 'windows\mosquitto\mosquitto.exe'
$taskRule = 'SimpleElevator-MQTT-WiFi'
try {
    $taskExisting = Get-NetFirewallRule -Name $taskRule -ErrorAction SilentlyContinue
    if ($taskExisting) { $taskExisting | Remove-NetFirewallRule }
    New-NetFirewallRule -Name $taskRule -DisplayName 'Simple Elevator - MQTT TCP 1883' -Direction Inbound -Action Allow -Protocol TCP -LocalPort 1883 -Profile Any | Out-Null
    'MQTT Wi-Fi firewall rule (TCP 1883) installed.' | Set-Content (Join-Path $taskRoot 'windows\logs\firewall-result.txt')
} catch {
    $_.Exception.Message | Set-Content (Join-Path $taskRoot 'windows\logs\firewall-result.txt')
    exit 1
}
