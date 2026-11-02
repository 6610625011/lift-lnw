# Stops only the local simulation processes started by start-local-sim.ps1.
Get-CimInstance Win32_Process -Filter "Name='node.exe'" |
    Where-Object { $_.CommandLine -like '*fake-esp32.cjs*' -or $_.CommandLine -like '*dev-broker.cjs*' -or ($_.CommandLine -like '*node-red\red.js*' -and $_.CommandLine -like '*windows\node-red*') } |
    ForEach-Object { Write-Host "stopping PID $($_.ProcessId)"; Stop-Process -Id $_.ProcessId -Force }
