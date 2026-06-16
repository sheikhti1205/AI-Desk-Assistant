$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Sketch = Join-Path $Root "AI Desk Assistant"
if (-not (Get-Command arduino-cli -ErrorAction SilentlyContinue)) {
    throw "arduino-cli is not available on PATH. Install Arduino CLI or use Arduino IDE first."
}
arduino-cli compile --fqbn esp32:esp32:esp32s3 $Sketch
