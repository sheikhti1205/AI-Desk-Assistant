# Smart Voice Assistant Project Report

## Cover Information

**Project Report:** Smart Voice Assistant

**Submitted By:**

| Name | ID |
|---|---|
| Safwan Ahmed Rayed | 24701066 |
| Sheikh Tawsif Bin Aftab | 24701026 |
| Syed Mehraz Monwar | 24701042 |
| Sanjida Mahi | 24701009 |

## Abstract

This project implements a compact smart voice assistant using an ESP32-S3, TFT display, microphone, local bridge server, and phone-hosted AI services. The assistant records a spoken command, sends it to a local server for speech-to-text and AI response generation, receives synthesized audio, and presents animated visual states on the display.

## Main Components

| Component | Purpose |
|---|---|
| ESP32-S3 | Main microcontroller and display/audio controller |
| ST7789 TFT display | Visual interface for clock, weather, system pages, and AI animations |
| MAX4466 microphone | Voice input |
| TTP223 touch sensor | Page cycling input |
| Push button | AI mode and recording control |
| HW-104 amplifier | Speaker output using right-channel input |
| XL6009 booster | Amplifier power supply boosting/regulation |
| Speaker | Audio playback |
| Windows PC | Local FastAPI bridge server |
| Android phone | Tool Neuron AI/STT/TTS provider |

## Operation

The device starts by loading Wi-Fi and setup configuration. Touch input cycles the normal display pages. A button press enters AI mode, the next press starts recording, and the third press stops recording and sends the audio to the local server. The server communicates with the Android AI service, then returns text and a generated audio URL to the ESP32-S3.

## Final Wiring Summary

The HW-104 amplifier uses `R` for audio input from ESP32-S3 GPIO4, `G` to shared ground, `L` disconnected, and `R+`/`R-` to the speaker. The XL6009 booster powers the amplifier through its output terminals, with shared ground connected across ESP32-S3, booster, and amplifier.

## Software Summary

The firmware is stored at `AI Desk Assistant\AI Desk Assistant.ino`. The server source is stored at `AI Server\server.py`. Runtime secrets and generated audio are intentionally not included in the cleaned final folder.

## Conclusion

The project demonstrates a working embedded voice assistant architecture where the ESP32-S3 handles interaction and display while heavier AI processing runs through a phone and local server bridge.
