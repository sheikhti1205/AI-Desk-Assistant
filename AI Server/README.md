# AI Server

This is the local bridge server for the ESP32-S3 AI Desk Assistant. It receives audio from the device, converts it into a WAV file, sends it to an OpenAI-compatible speech-to-text endpoint, generates a chat response, creates speech audio, and returns the result to the ESP32-S3.

## Role In The Project

The ESP32-S3 is excellent for display control, simple audio capture, Wi-Fi, and playback, but the AI work is handled off-device. This server sits between the ESP32-S3 and the Android AI provider:

```text
ESP32-S3 -> FastAPI server -> Tool Neuron / OpenAI-compatible API
ESP32-S3 <- audio URL + response text <- FastAPI server
```

## Files

| File | Purpose |
|---|---|
| `server.py` | FastAPI app for voice requests, provider calls, audio conversion, and fallback replies |
| `requirements.txt` | Python package list |
| `.env.example` | Example configuration file |

## Main Features

- Accepts short raw audio recordings from the ESP32-S3.
- Converts ESP32-friendly raw 8-bit audio into WAV.
- Sends audio to Tool Neuron STT through `/audio/transcriptions`.
- Sends text prompts to a chat model through `/chat/completions`.
- Sends generated replies to Tool Neuron TTS through `/audio/speech`.
- Converts generated speech to 8 kHz unsigned 8-bit mono WAV for ESP32 playback.
- Serves generated audio files back to the ESP32 through `/audio/...`.
- Falls back to local/simple replies when AI services are unavailable.

## Configuration

Create a local `.env` file from `.env.example`:

```powershell
Copy-Item "AI Server\.env.example" "AI Server\.env"
notepad "AI Server\.env"
```

Typical values:

```env
TOOL_NEURON_BASE_URL=http://PHONE-IP:11434/v1
TOOL_NEURON_API_KEY=your-local-token
TOOL_NEURON_CHAT_MODEL=your-chat-model-id
TOOL_NEURON_STT_MODEL=your-stt-model-id
TOOL_NEURON_TTS_MODEL=your-tts-model-id
TOOL_NEURON_TTS_VOICE=amy
TOOL_NEURON_MAX_TOKENS=384
BUDDY_MAX_RESPONSE_CHARS=1600
PUBLIC_BASE_URL=http://PC-IP:3000
PORT=3000
```

`PUBLIC_BASE_URL` must be the PC address that the ESP32-S3 can reach on the local network.

## Install And Run

From the repository root:

```powershell
python -m pip install -r "AI Server\requirements.txt"
.\Setup Guide\Start-Server.ps1
```

Or run directly:

```powershell
python -m uvicorn server:app --host 0.0.0.0 --port 3000 --app-dir "AI Server"
```

## API Used By The Device

The firmware sends voice recordings to the server's voice endpoint. The server response includes the transcript, AI reply text, and an audio URL that the ESP32-S3 can download and play.

The server also serves audio through:

```text
http://PC-IP:3000/audio/<generated-file>.wav
```

## Dependencies

Core packages:

- FastAPI
- Uvicorn
- python-multipart
- python-dotenv
- OpenAI Python SDK
- Google GenAI SDK
- faster-whisper

Tool Neuron is the intended local Android AI provider, but the server also contains fallback paths for other configured providers.

## Security Note

Do not commit `.env` with real API keys or local bearer tokens. Keep only `.env.example` in the repository.
