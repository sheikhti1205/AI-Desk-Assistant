$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$ServerDir = Join-Path $Root "AI Server"
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw "Python is not available on PATH. Install Python 3.11+ first."
}
Push-Location $ServerDir
try {
    python -c "import server; print('Server source imports successfully')"
}
finally {
    Pop-Location
}
