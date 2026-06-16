# Project Notes

The Smart Voice Assistant is an ESP32-S3 desk companion with a TFT interface, microphone input, touch/button controls, local PC bridge server, and phone-hosted OpenAI-compatible AI services.

## Final Interaction Flow

- Touch cycles display pages such as clock, weather, and system information.
- First button press enters AI mode.
- Second button press starts recording.
- Third button press stops recording and sends the voice request to the local server.
- While waiting for AI response, the interface shows the red rotating/sizing waiting animation.
- During speaking, the AI face changes to the speaking color animation.

## Final UI Direction

- Anime.js-inspired square-ish frame style.
- Bold dot matrix for clock/time display in 12-hour mode.
- AI listening face uses bouncing dots.
- Boot/setup can show blue staggered dot animation or setup QR on the same frame.
- Weather UI should use icon-style animated weather states and no forecast section.

## Cleanup State

This final folder is cleaned for storage/submission. Runtime config, private credentials, uploaded audio, generated audio, Python environments, Arduino tool caches, and old experiments were removed.

