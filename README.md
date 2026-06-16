# Smart Voice Assistant

This folder is the cleaned final copy of the ESP32-S3 Smart Voice Assistant project.

## Folder Map

```text
IoT_AI_Desk_Buddy
├─ README.md
├─ AI Desk Assistant
│  └─ AI Desk Assistant.ino
├─ AI Server
│  ├─ server.py
│  ├─ requirements.txt
│  ├─ README.md
│  └─ .env.example
├─ Setup Guide
│  ├─ README.md
│  ├─ Configure-Server.ps1
│  ├─ Start-Server.ps1
│  ├─ Stop-Server.ps1
│  ├─ Test-AI.ps1
│  ├─ Build-Firmware.ps1
│  └─ Flash-Firmware.ps1
└─ Documentation
   ├─ Wiring.md
   ├─ Project Notes.md
   ├─ Troubleshooting.md
   ├─ Submission Procedure.md
   └─ Report
      ├─ ESP32_AI_Voice_Assistant_Report.docx
      ├─ ESP32_AI_Voice_Assistant_Report.pdf
      ├─ SMART_VOICE_ASSISTANT_PROJECT_REPORT.md
      └─ report_assets
```

## What Is Preserved

- Final ESP32-S3 Arduino sketch: `AI Desk Assistant\AI Desk Assistant.ino`
- Clean local AI server source: `AI Server\server.py`
- Server dependency list: `AI Server\requirements.txt`
- Token-free example configuration: `AI Server\.env.example`
- Setup helper scripts under `Setup Guide`
- Final project report files under `Documentation\Report`
- Equipment images used by the report under `Documentation\Report\report_assets`

## What Was Removed

The cleanup intentionally removed private, generated, runtime, and old experiment files:

- Server `.env` files and private credentials
- Uploaded/request audio and generated response audio
- Server logs and Python cache folders
- Python virtual environments such as `.venv`
- Arduino/tool cache folders such as `.tools` and `.build`
- Old `archive`, `assets`, `refs`, `report_work`, `_private`, `tools`, test folders, and duplicate root documents

## Reviving The Project Later

This copy is clean for submission/storage. To run it again later, reinstall the missing tools first:

1. Install Python 3.11+.
2. From the root folder, run `Setup Guide\Configure-Server.ps1` and fill in a new local AI server URL/API key if needed.
3. Install server packages with `python -m pip install -r "AI Server\requirements.txt"`.
4. Run `Setup Guide\Start-Server.ps1`.
5. Install Arduino IDE or Arduino CLI and the ESP32 board package.
6. Open `AI Desk Assistant\AI Desk Assistant.ino` in Arduino IDE, or use the build/flash scripts after installing `arduino-cli`.

No private phone/server token is stored in this folder.

