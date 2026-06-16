# Wiring

Final hardware wiring for the last assembled version.

| Module | Pin/Terminal | Connects To |
|---|---|---|
| ESP32-S3 | GPIO4 | HW-104 `R` audio input |
| ESP32-S3 | GND | HW-104 `G`, XL6009 `IN-`, shared ground |
| ESP32-S3 | 5V/VIN | Main 5V supply if powered from the same rail |
| HW-104 amplifier | `R` | ESP32-S3 GPIO4 audio output |
| HW-104 amplifier | `G` | Shared GND |
| HW-104 amplifier | `L` | Disconnected |
| HW-104 amplifier | `R+` | Speaker positive |
| HW-104 amplifier | `R-` | Speaker negative |
| XL6009 booster | `IN+` | Battery/supply positive |
| XL6009 booster | `IN-` | Battery/supply negative / shared GND |
| XL6009 booster | `OUT+` | HW-104 amplifier 5V positive |
| XL6009 booster | `OUT-` | HW-104 amplifier 5V negative / shared GND |
| Speaker | `+` | HW-104 `R+` |
| Speaker | `-` | HW-104 `R-` |

Use a common ground between ESP32-S3, amplifier, and booster output. Keep the amplifier supply at its expected voltage range.
