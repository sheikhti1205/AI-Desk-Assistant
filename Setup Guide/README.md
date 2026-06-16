# Setup Guide

This folder contains helper scripts for running the PC server and building/flashing the ESP32-S3 firmware.

## Requirements

Install these before running the full project:

- Python 3.11 or newer
- Arduino IDE or Arduino CLI
- ESP32 board support for Arduino
- Android phone running Tool Neuron or another OpenAI-compatible local AI server
- ESP32-S3 connected over USB for flashing
- Windows PC and ESP32-S3 on the same Wi-Fi network during use

## 1. Configure The AI Server

From the repository root, create the local server config:

```powershell
.\Setup Guide\Configure-Server.ps1
```

This creates `AI Server\.env` from `AI Server\.env.example` and opens it in Notepad.

Fill in values like:

```env
TOOL_NEURON_BASE_URL=http://PHONE-IP:11434/v1
TOOL_NEURON_API_KEY=your-local-token
TOOL_NEURON_CHAT_MODEL=your-chat-model-id
TOOL_NEURON_STT_MODEL=your-stt-model-id
TOOL_NEURON_TTS_MODEL=your-tts-model-id
PUBLIC_BASE_URL=http://PC-IP:3000
PORT=3000
```

Use the PC's LAN IP for `PUBLIC_BASE_URL`, because the ESP32-S3 needs to download generated audio from that address.

## 2. Install Python Dependencies

From the repository root:

```powershell
python -m pip install -r "AI Server\requirements.txt"
```

## 3. Start The Server

```powershell
.\Setup Guide\Start-Server.ps1
```

The server listens on:

```text
http://0.0.0.0:3000
```

Use `http://PC-IP:3000` from the ESP32-S3.

To stop it:

```powershell
.\Setup Guide\Stop-Server.ps1
```

## 4. Build The Firmware

With Arduino IDE:

1. Open `AI Desk Assistant\AI Desk Assistant.ino`.
2. Select your ESP32-S3 board.
3. Select the correct COM port.
4. Install the required display/ESP32 libraries if Arduino IDE asks for them.
5. Click Verify.

With Arduino CLI:

```powershell
.\Setup Guide\Build-Firmware.ps1
```

## 5. Flash The Firmware

With Arduino IDE, click Upload.

With Arduino CLI:

```powershell
.\Setup Guide\Flash-Firmware.ps1 -Port COM3
```

Change `COM3` to the actual ESP32-S3 port.

## 6. Device Setup Flow

On first setup or after clearing saved Wi-Fi:

1. The ESP32-S3 starts a setup access point named `AI_Buddy_Setup`.
2. Connect to it from a phone or PC.
3. Use password `12345678`.
4. Enter Wi-Fi SSID, Wi-Fi password, and local server URL.
5. Save and reboot/connect.

The local server URL should look like:

```text
http://PC-IP:3000
```

## Controls

| Input | Action |
|---|---|
| Touch sensor | Cycle through display pages |
| Button press 1 | Enter AI mode |
| Button press 2 | Start recording |
| Button press 3 | Stop recording and send voice |
| Long press | Setup/utility behavior depending on current firmware state |

## Scripts

| Script | Purpose |
|---|---|
| `Configure-Server.ps1` | Creates/opens `AI Server\.env` |
| `Start-Server.ps1` | Starts the FastAPI server |
| `Stop-Server.ps1` | Stops the process listening on port 3000 |
| `Test-AI.ps1` | Checks whether the Python server source imports |
| `Build-Firmware.ps1` | Compiles the Arduino sketch with Arduino CLI |
| `Flash-Firmware.ps1` | Uploads the Arduino sketch with Arduino CLI |

## Common Checks

- The phone AI server must be running before voice requests work.
- The server base URL for Tool Neuron should usually end with `/v1`.
- Windows Firewall may need to allow Python/port 3000.
- ESP32-S3, phone, and PC should be on the same network.
- The HW-104 amplifier, XL6009 booster, and ESP32-S3 must share ground.
