# Troubleshooting

## Server Does Not Start

- Install Python 3.11+.
- Install packages: `python -m pip install -r "AI Server\requirements.txt"`.
- Create fresh config with `Setup Guide\Configure-Server.ps1`.
- Start with `Setup Guide\Start-Server.ps1`.

## ESP32 Cannot Reach Server

- Confirm the PC and ESP32 are on the same network.
- Use the PC LAN IP in the ESP32 setup page, for example `http://PC-IP:3000`.
- Allow Python through Windows Firewall if prompted.

## No Speaker Output

- Confirm HW-104 `R` is connected to ESP32 GPIO4.
- Confirm HW-104 `G` and ESP32 GND share ground.
- Confirm XL6009 output powers the amplifier at the correct voltage.
- Confirm speaker is connected to `R+` and `R-`.

## AI Calls Fail

- Confirm the phone AI app is running.
- Confirm the OpenAI-compatible base URL ends with `/v1`.
- Put a fresh token in `AI Server\.env` only when reviving the project.
- Check available phone models and update `.env` model names if needed.
