<!--
DCF77 RC8000 Console v2.5.4
Copyright (c) 2026 Gianpaolo P.
Licensed under the PolyForm Noncommercial License 1.0.0.
Commercial use requires a separate written license from the copyright holder.
See LICENSE and NOTICE.md.
-->

# Hardware — v2.5.4

## Components

The reference setup contains **two separate devices**:

1. **HW-364A**: ESP8266 board with integrated 0.96" 128x64 OLED.
2. **RC8000 / DCF-3850N-800**: external 77.5 kHz DCF77 receiver with DATA output and PON control.

This distinction matters: the HW-364A is the controller board, not the DCF77 radio receiver.

## Firmware pinout

| Signal | ESP8266 GPIO | Direction | State / behavior |
|---|---:|---|---|
| `DCF_DATA_PIN` | 13 | IN | receiver pulses; plain `INPUT` by default |
| `DCF_PON_PIN` | 5 | OUT | active LOW: LOW=ON, HIGH=OFF |
| `OLED_SDA_PIN` | 14 | I2C | integrated OLED SDA |
| `OLED_SCL_PIN` | 12 | I2C | integrated OLED SCL |
| OLED address | — | I2C | 0x3C default; also probes 0x3D |

The firmware also probes swapped SDA/SCL (GPIO12/GPIO14) because inconsistent HW-364A batches/documentation have been observed.

## OLED

Primary target is SSD1306 128x64, with SH1106 fallback. During DCF77 acquisition the display is turned off; on SSD1306 the charge pump is also disabled to further reduce panel-generated noise.

## DCF77 receiver assumptions

The firmware assumes an external receiver that provides:

- 77.5 kHz DCF77 reception;
- digital DATA pulses;
- active-low PON;
- 3.3 V operation in the reference build;
- common ground with the ESP8266.

## Power and decoupling

DCF77 reception is susceptible to local electrical noise. Keep decoupling physically close to the receiver supply pins (for example a 100 nF ceramic plus suitable bulk capacitance), avoid long ground loops, and keep switching supplies away from the ferrite antenna when possible.

## Observed EMI sensitivities

The tested assembly was affected by:

- OLED charge-pump and I2C activity;
- Wi-Fi activity, especially network scanning;
- ESP8266/digital wiring close to the ferrite antenna;
- USB/switching power noise;
- ferrite antenna orientation.

This is why v2.5.4 uses strict temporal separation: **radio ON = OLED OFF**, **radio OFF = OLED ON**.

## HW-364A references

Public working examples report a 128x64 OLED at 0x3C on GPIO14/GPIO12. D5/D6 labels are inconsistently reported, so this project treats GPIO numbers as authoritative.

- https://github.com/Bl4d3hUnt3r/HW364-A
- https://github.com/dzwiedziu-nkg/nodemcu-with-oled-example
