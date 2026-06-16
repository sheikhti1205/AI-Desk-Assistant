# AI Desk Assistant

An ESP32-S3 based smart voice assistant with a round TFT-style interface, microphone input, speaker output, touch/button controls, and a local AI bridge server. The device records short voice commands, sends them to a PC server, uses an Android-hosted OpenAI-compatible AI service for speech-to-text, chat, and text-to-speech, then plays the response through the desk assistant.

## What It Does

- Shows a small animated desk UI on a 240x240 ST7789 display.
- Cycles through clock, weather, system, and AI pages with touch input.
- Uses a button-driven voice flow:
  1. First press enters AI mode.
  2. Second press starts recording.
  3. Third press stops recording and sends the voice request.
- Sends recorded audio to a local FastAPI server.
- The server calls Tool Neuron or another OpenAI-compatible backend for STT, chat, and TTS.
- The ESP32-S3 receives the transcript/response/audio URL and plays an 8 kHz WAV response.
- Includes setup-mode support through an ESP32 access point and setup page.

## Hardware

| Part | Role |
|---|---|
| ESP32-S3 | Main controller for UI, Wi-Fi, mic sampling, and speaker PWM output |
| ST7789 240x240 TFT | Circular/square-ish animated display interface |
| MAX4466 microphone module | Voice input |
| TTP223 touch sensor | Page cycling |
| Push button | AI mode and recording control |
| HW-104 amplifier | Speaker amplifier using right-channel input |
| XL6009 booster | Amplifier power supply |
| Speaker | Voice playback |
| Windows PC | Runs the local AI bridge server |
| Android phone | Runs Tool Neuron / OpenAI-compatible AI endpoints |

## Pin Map

| ESP32-S3 Pin | Connected Device |
|---|---|
| GPIO1 | MAX4466 microphone analog output |
| GPIO2 | Test/status LED |
| GPIO4 | HW-104 `R` audio input |
| GPIO5 | TTP223 touch output |
| GPIO6 | Push button |
| GPIO9 | TFT DC |
| GPIO11 | TFT MOSI |
| GPIO12 | TFT SCK |
| GND | Shared ground for ESP32, amplifier, booster, and sensors |

More wiring detail is in [`Documentation/Wiring.md`](Documentation/Wiring.md).

## Software Architecture

```text
Voice input
   |
   v
ESP32-S3 firmware
   |
   | raw 8-bit audio over HTTP
   v
Local FastAPI server on Windows
   |
   | OpenAI-compatible API calls
   v
Android Tool Neuron AI service
   |
   v
STT -> chat response -> TTS WAV
   |
   v
ESP32-S3 downloads and plays audio
```

The ESP32-S3 handles the embedded experience: UI pages, setup portal, recording, upload, response display, and audio playback. The Windows server handles the heavier API work and audio conversion. The Android phone hosts the local AI models through an OpenAI-compatible `/v1` API.

## Repository Layout

```text
AI Desk Assistant/
  AI Desk Assistant.ino       Arduino firmware

AI Server/
  server.py                   FastAPI bridge server
  requirements.txt            Python dependencies
  .env.example                Example server config

Setup Guide/
  README.md                   Setup and run instructions
  *.ps1                       Windows helper scripts

Documentation/
  Wiring.md                   Hardware wiring table
  Project Notes.md            Interaction/UI notes
  Troubleshooting.md          Common fixes
  AI_Desk_Assistant_Submission_Document.docx
                              Submit-ready project documentation
  Report/                     Project report and equipment images
```

## Quick Start

1. Install Python 3.11+ on the Windows PC.
2. Install the server dependencies:

   ```powershell
   python -m pip install -r "AI Server\requirements.txt"
   ```

3. Copy `AI Server\.env.example` to `AI Server\.env` and fill in the phone AI server URL/model values.
4. Start the PC server:

   ```powershell
   .\Setup Guide\Start-Server.ps1
   ```

5. Install Arduino IDE or Arduino CLI with ESP32 board support.
6. Open `AI Desk Assistant\AI Desk Assistant.ino`, select the ESP32-S3 board/port, and upload.
7. If Wi-Fi/server details need to be changed, enter setup mode on the device and use the `AI_Buddy_Setup` access point.

Detailed setup steps are in [`Setup Guide/README.md`](Setup%20Guide/README.md).

## Server Configuration

The server reads settings from `AI Server\.env`. Start from the example file:

```env
TOOL_NEURON_BASE_URL=http://PHONE-IP:11434/v1
TOOL_NEURON_API_KEY=
TOOL_NEURON_CHAT_MODEL=
TOOL_NEURON_STT_MODEL=
TOOL_NEURON_TTS_MODEL=
TOOL_NEURON_TTS_VOICE=amy
PUBLIC_BASE_URL=http://PC-IP:3000
PORT=3000
```

`PUBLIC_BASE_URL` should be reachable by the ESP32-S3 on the same network. `TOOL_NEURON_BASE_URL` should point to the Android AI service's OpenAI-compatible `/v1` endpoint.

## Project Report

The prepared report is available in:

- [`Documentation/Report/ESP32_AI_Voice_Assistant_Report.docx`](Documentation/Report/ESP32_AI_Voice_Assistant_Report.docx)
- [`Documentation/Report/ESP32_AI_Voice_Assistant_Report.pdf`](Documentation/Report/ESP32_AI_Voice_Assistant_Report.pdf)

## Notes

This repository does not include private runtime credentials. Create your own `.env` locally when running the server.
