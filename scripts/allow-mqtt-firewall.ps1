# Run elevated once. Only Mosquitto on Wi-Fi, TCP 1883, local subnet is allowed.
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskProgram = Join-Path $taskRoot 'windows\mosquitto\mosquitto.exe'
$taskRule = 'SimpleElevator-MQTT-WiFi'
try {
    $taskExisting = Get-NetFirewallRule -Name $taskRule -ErrorAction SilentlyContinue
    if ($taskExisting) { $taskExisting | Remove-NetFirewallRule }
    if (Get-NetIPAddress -AddressFamily IPv4 -IPAddress '192.168.137.1' -ErrorAction SilentlyContinue) {
        New-NetFirewallRule -Name $taskRule -DisplayName 'Simple Elevator - MQTT from private hotspot' -Direction Inbound -Action Allow -Protocol TCP -LocalPort 1883 -Program $taskProgram -LocalAddress '192.168.137.1' -RemoteAddress '192.168.137.0/24' -Profile Any | Out-Null
    } else {
        New-NetFirewallRule -Name $taskRule -DisplayName 'Simple Elevator - MQTT from local Wi-Fi' -Direction Inbound -Action Allow -Protocol TCP -LocalPort 1883 -Program $taskProgram -InterfaceAlias 'Wi-Fi' -RemoteAddress LocalSubnet -Profile Any | Out-Null
    }
    'MQTT Wi-Fi firewall rule installed.' | Set-Content (Join-Path $taskRoot 'windows\logs\firewall-result.txt')
} catch {
    $_.Exception.Message | Set-Content (Join-Path $taskRoot 'windows\logs\firewall-result.txt')
    exit 1
}
