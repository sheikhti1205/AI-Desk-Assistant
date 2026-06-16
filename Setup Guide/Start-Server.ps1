$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$ServerDir = Join-Path $Root "AI Server"
$EnvPath = Join-Path $ServerDir ".env"
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw "Python is not available on PATH. Install Python 3.11+ first."
}
if (-not (Test-Path -LiteralPath $EnvPath)) {
    Write-Warning "AI Server\.env does not exist. Run Setup Guide\Configure-Server.ps1 before using real AI calls."
}
python -m uvicorn server:app --host 0.0.0.0 --port 3000 --app-dir $ServerDir
