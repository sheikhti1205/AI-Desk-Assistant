$ErrorActionPreference = "Stop"
$connection = Get-NetTCPConnection -LocalPort 3000 -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $connection) {
    Write-Host "No process is listening on port 3000."
    exit 0
}
$process = Get-Process -Id $connection.OwningProcess -ErrorAction Stop
Write-Host "Stopping $($process.ProcessName) on port 3000 (PID $($process.Id))"
Stop-Process -Id $process.Id -Force
