# ESP32 DCF77 Diagnostic

Portable long-wave time-signal analyzer for the classic **LILYGO / TTGO T-Display ESP32** with integrated **1.14-inch ST7789 240x135 TFT**.

The project started as a DCF77 diagnostic receiver and now supports two hardware profiles:

1. **generic single-frequency DCF77 receiver** at 77.5 kHz;
2. **C-MAX CMMR-6D-7760 dual-frequency receiver** at 60 / 77.5 kHz.

At **77.5 kHz** the firmware performs complete DCF77 decoding. At **60 kHz** it operates as a protocol-neutral timing/signal analyzer so that MSF, WWVB or JJY60 signals are not incorrectly interpreted as DCF77.

## Main functions

- DCF77 digital pulse decoder (100 ms = 0, 200 ms = 1)
- automatic DCF77 minute-marker detection
- minute/hour/date parity checks (P1/P2/P3)
- CET / CEST detection
- DST-change and leap-second announcement bits
- pulse width and second-period measurement in microseconds
- instantaneous jitter and 60-s RMS jitter
- rolling timing quality score
- average DCF77 0-bit / 1-bit pulse duration
- valid/invalid pulse and frame counters
- last 59-bit DCF77 frame visualization
- 60-kHz RAW diagnostic mode
- dual-band 60 / 77.5 kHz switching for C-MAX CMMR-6D-7760
- receiver settling guard after a band change
- optional analog envelope input
- optional GPS PPS input for receiver-vs-PPS offset measurements
- serial CSV-like diagnostic logging
- four TFT pages selected with the built-in button

## Hardware target

Classic LILYGO / TTGO T-Display ESP32:

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

Receiver pins:

| Function | GPIO | Notes |
|---|---:|---|
| Receiver DATA | 27 | generic OUT or C-MAX TCO/TCON |
| C-MAX BAND | 25 | dual build only |
| C-MAX PON | 26 | dual build only, active low |
| Optional analog envelope | 32 | disabled by default |
| Optional GPS PPS | 33 | disabled by default |

## Build profiles

### Generic 77.5 kHz DCF77 module

```bash
pio run -e ttgo-t-display
pio run -e ttgo-t-display -t upload
```

### C-MAX CMMR-6D-7760 dual 60 / 77.5 kHz

```bash
pio run -e ttgo-t-display-dual
pio run -e ttgo-t-display-dual -t upload
```

The two profiles are also compiled automatically by GitHub Actions on pushes and pull requests.

## C-MAX dual-frequency operation

For the EU 60/77.5 kHz C-MAX receiver:

- `BAND = GND` selects **77.5 kHz**
- `BAND = VDD` selects **60 kHz**
- `PON` is active-low
- `TCO` is the positive data output
- `TCON` is the inverted data output
- `HLD` controls AGC hold

The dual build uses:

```text
GPIO25 -> BAND
GPIO26 -> PON
GPIO27 <- TCO or TCON
```

After changing frequency the firmware ignores received pulses for 3.5 seconds so the receiver can settle.

### Controls

```text
GPIO35 short press   next display page
GPIO35 long press    toggle 77.5 / 60 kHz
GPIO0 press          TFT backlight

Serial:
7                    select 77.5 kHz
6                    select 60.0 kHz
b                    toggle frequency
```

## 77.5 kHz mode

The main screen provides decoded DCF77 time/date plus:

- signal/timing quality
- bit number/value
- pulse width
- period
- jitter
- parity
- frame-lock statistics

## 60 kHz mode

The screen changes to **60.0 kHz RAW SIGNAL MONITOR**.

It intentionally does not assign DCF77 bit values. Instead it measures:

- raw pulse width
- one-second periodicity
- instantaneous jitter
- RMS jitter
- valid/invalid timing events
- glitches
- rolling quality
- optional PPS offset

This mode is suitable for antenna and front-end diagnostics before adding specific MSF/WWVB/JJY60 protocol decoders.

See [docs/DUAL_FREQUENCY.md](docs/DUAL_FREQUENCY.md).

## DCF77 receiver polarity

Default:

```cpp
constexpr bool DCF77_ACTIVE_LOW = true;
```

For C-MAX, select either TCO or TCON and set the polarity accordingly. If the pulse widths are implausible or the input seems continuously asserted, use the complementary output or invert this setting.

## GPS PPS

Without an independent reference, the analyzer can measure pulse timing and one-second stability but cannot determine absolute arrival-time offset.

Enable `PPS_ENABLED` and connect a GPS 1-PPS output to GPIO33 to display receiver-edge offset versus PPS.

## RF / EMC notes

77.5 kHz and 60 kHz ferrite reception are sensitive to local interference. Keep the antenna and analog receiver away from:

- ESP32 and TFT electronics
- USB/DC-DC converters
- switching supplies
- LED drivers
- PWM wiring
- large metal objects

For a serious diagnostic instrument, place the ferrite/front-end 15-30 cm from the ESP32/TFT board and use a clean analog supply.

## Documentation

- [Wiring](docs/WIRING.md)
- [DCF77 frame](docs/DCF77_FRAME.md)
- [Dual-frequency receiver support](docs/DUAL_FREQUENCY.md)
- PTB DCF77: https://www.ptb.de/cms/en/ptb/fachabteilungen/abt4/fb-44/ag-442/dissemination-of-legal-time/dcf77.html
- C-MAX CMMR-6 receiver specification: https://science.mainguet.org/tech/dcf77/CMax_CMMR6.pdf

## License

MIT - Gianpaolo Paglialunga
