# Setup Guide

These scripts are kept as clean helper templates. The local Python environment, Arduino tools, build cache, server `.env`, uploads, and audio files were removed during cleanup.

Before using the scripts again:

1. Install Python 3.11+ and make sure `python` is available in PowerShell.
2. Run `python -m pip install -r "AI Server\requirements.txt"` from the project root.
3. Run `Configure-Server.ps1` and enter fresh server values in `AI Server\.env`.
4. Install Arduino CLI or Arduino IDE with ESP32 board support if you need firmware build/flash commands.

## Scripts

- `Configure-Server.ps1` - creates `AI Server\.env` from `.env.example` and opens it in Notepad.
- `Start-Server.ps1` - starts the FastAPI server on port 3000 using system Python.
- `Stop-Server.ps1` - stops whatever is listening on port 3000.
- `Test-AI.ps1` - performs a lightweight server import check.
- `Build-Firmware.ps1` - compiles the Arduino sketch if `arduino-cli` is installed.
- `Flash-Firmware.ps1` - uploads the sketch if `arduino-cli` is installed and the board is connected.
