# AI Server

This folder contains the cleaned FastAPI server used by the ESP32-S3 assistant.

## Files

- `server.py` - HTTP API for voice upload, STT, text generation, TTS, and audio serving.
- `requirements.txt` - Python packages required by the server.
- `.env.example` - token-free example settings.

## Important

Runtime files were removed during cleanup:

- `.env`
- `uploads`
- `public/audio`
- `*.log`
- `__pycache__`
- `.venv`

Create a fresh `.env` only if you revive the device later. Do not commit or share real bearer tokens.
