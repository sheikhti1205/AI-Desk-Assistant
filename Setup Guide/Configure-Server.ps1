$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$EnvPath = Join-Path $Root "AI Server\.env"
$ExamplePath = Join-Path $Root "AI Server\.env.example"
if (-not (Test-Path -LiteralPath $EnvPath)) {
    Copy-Item -LiteralPath $ExamplePath -Destination $EnvPath
    Write-Host "Created AI Server\.env from .env.example. Fill in fresh local values before running the server."
}
notepad.exe $EnvPath
