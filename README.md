# DCF77 Signal Analyzer - TTGO T-Display

Portable DCF77 receiver/diagnostic instrument for the classic **LILYGO / TTGO T-Display ESP32** with integrated **1.14-inch ST7789 240x135 TFT**.

The goal is not only to show the radio-controlled clock, but to expose reception quality and timing diagnostics in real time.

## Main functions

- DCF77 digital pulse decoder (100 ms = 0, 200 ms = 1)
- automatic minute-marker detection
- minute/hour/date parity checks (P1/P2/P3)
- CET / CEST detection
- DST-change and leap-second announcement bits
- pulse width and second-period measurement in microseconds
- instantaneous jitter and 60-s RMS jitter
- rolling timing quality score
- average 0-bit / 1-bit pulse duration
- valid/invalid pulse and frame counters
- last 59-bit frame visualization
- optional analog envelope input
- optional GPS PPS input for real DCF77-vs-PPS offset measurement
- serial CSV-like diagnostic logging
- four TFT pages selected with the built-in GPIO35 button

## Hardware target

LILYGO documentation lists the classic T-Display as ESP32 + ST7789V, 240x135 pixels, with:

| Function | GPIO |
|---|---:|
| TFT MOSI | 19 |
| TFT SCLK | 18 |
| TFT CS | 5 |
| TFT DC | 16 |
| TFT RST | 23 |
| TFT Backlight | 4 |
| Button 1 | 35 |
| Button 2 / BOOT | 0 |

Project-specific pins:

| Function | GPIO | Default |
|---|---:|---|
| DCF77 digital output | 27 | enabled |
| Optional analog envelope | 32 | disabled |
| Optional GPS PPS | 33 | disabled |

All project-specific pins and polarity are in `include/config.h`.

## DCF77 receiver connection

Typical three-wire receiver module:

```text
DCF77 receiver       TTGO T-Display
-----------------------------------
VCC              ->  module-specific supply
GND              ->  GND
DATA / OUT        ->  GPIO27
```

**Do not assume the receiver supply voltage.** Some modules are 3.3 V compatible, others are designed for lower supply voltages. Ensure the DATA level presented to ESP32 never exceeds 3.3 V.

If the output is open collector, the default project enables the ESP32 internal pull-up. For a long cable or a noisy environment, an external 4.7k-10k pull-up to 3.3 V is usually preferable.

## Why GPS PPS is optional

Without an independent time reference, the analyzer can accurately measure:

- 100/200 ms pulse widths
- one-second periodicity
- jitter relative to 1.000000 s
- missing / malformed marks
- frame integrity

It cannot determine the absolute arrival-time offset of DCF77. Enable `PPS_ENABLED` and feed a GPS 1-PPS output to GPIO33 to display DCF77 edge offset versus PPS.

## Display pages

**Page 1 - Overview**

- decoded local time/date
- CET / CEST
- lock status
- timing quality bar
- current bit, pulse width and jitter

**Page 2 - Signal / Timing**

- pulse width
- period
- instantaneous jitter
- RMS jitter
- PPS offset when enabled
- timing/glitch counters

**Page 3 - Last Minute Frame**

- all 59 DCF77 bits
- P1/P2/P3 status
- last decoded timestamp
- valid/invalid frames

**Page 4 - Statistics**

- average pulse-0 duration
- average pulse-1 duration
- average one-second period
- pulse/frame counters
- parity errors

Button GPIO35 changes page. Button GPIO0 toggles the TFT backlight.

## Build and upload

Install VS Code + PlatformIO, open this directory, then:

```bash
pio run
pio run -t upload
pio device monitor
```

The project configures TFT_eSPI entirely from `platformio.ini`, so no manual edit inside the library is required.

## Receiver polarity

Default:

```cpp
constexpr bool DCF77_ACTIVE_LOW = true;
```

If the displayed pulse lengths make no sense or the signal appears continuously active, change it to `false`.

## Decoder timing

The PTB DCF77 AM time code uses nominal 0.1 s marks for binary 0 and 0.2 s marks for binary 1. The final normal second mark of a minute is omitted, creating the minute boundary used by this analyzer for frame synchronization.

The acceptance windows in `config.h` are deliberately wider than the nominal values so the instrument can diagnose a marginal receiver instead of immediately discarding everything.

## RF / EMC notes

77.5 kHz reception is sensitive to local interference. Keep the ferrite antenna and analog receiver physically away from:

- ESP32 itself
- TFT flex/display electronics
- USB/DC-DC converters
- switching power supplies
- PWM wiring

For a serious active receiver, use a separate low-noise LDO for the analog stage, local decoupling, short analog traces and preferably 15-30 cm physical separation between ferrite/front-end and the ESP32/TFT board.

## Reference documentation

- PTB DCF77: https://www.ptb.de/cms/en/ptb/fachabteilungen/abt4/fb-44/ag-442/dissemination-of-legal-time/dcf77.html
- PTB DCF77 time-code technical material: https://www.ptb.de/cms/fileadmin/internet/fachabteilungen/abteilung_4/4.4_zeit_und_frequenz/pdf/2011_PTBMitt_50a_DCF77_engl.pdf
- LILYGO T-Display documentation: https://github.com/Xinyuan-LilyGO/documentation/blob/master/en/products/t-display-series/t-display/index.md

## Next hardware step

Version 1 expects a demodulated DCF77 receiver output. A later hardware revision can add a dedicated 77.5 kHz active front-end with ferrite resonator, high-Q filtering, AGC/envelope output and a clean digital comparator. That would make the ENV value a real reception-strength/noise diagnostic rather than just a digital timing analyzer.
