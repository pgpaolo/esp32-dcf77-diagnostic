# Wiring

## 1. Generic single-frequency DCF77 receiver

```text
DCF77 receiver       TTGO T-Display
-----------------------------------
DATA / OUT        -> GPIO27
GND               -> GND
VCC               -> receiver-specific supply
```

Build:

```bash
pio run -e ttgo-t-display
```

Do not assume the receiver supply voltage. Ensure the DATA level presented to the ESP32 never exceeds 3.3 V.

## 2. C-MAX CMMR-6D-7760 dual-frequency receiver

The C-MAX dual EU version supports **60.0 kHz / 77.5 kHz**. The receiver's `BAND` input selects the receiver path:

- `BAND = GND` -> higher frequency -> **77.5 kHz**
- `BAND = VDD` -> lower frequency -> **60.0 kHz**

Recommended 3.3 V connection:

```text
CMMR-6D-7760          TTGO T-Display
------------------------------------
VDD                -> 3V3
GND                -> GND
BAND               -> GPIO25
PON                -> GPIO26
TCO or TCON        -> GPIO27
HLD                -> 3V3
IN1 / IN2          -> matched dual-frequency ferrite antenna
```

Build:

```bash
pio run -e ttgo-t-display-dual
```

### TCO / TCON polarity

The module provides both a positive output (`TCO`) and an inverted output (`TCON`). The firmware defaults to active-low input. Choose the output and polarity together; if pulse widths are nonsensical, use the complementary output or change `DCF77_ACTIVE_LOW`.

### PON and HLD

C-MAX `PON` is active-low. In the dual build GPIO26 is driven LOW to enable the receiver.

Version 1 does not control AGC hold in software. Tie `HLD` to VDD so AGC remains enabled.

### Band switching

On the T-Display:

- short press GPIO35 button -> next screen
- long press GPIO35 button (>=1.2 s) -> switch 77.5 / 60 kHz
- GPIO0 button -> TFT backlight

Serial commands:

```text
7   77.5 kHz / DCF77
6   60.0 kHz / RAW monitor
b   toggle band
```

After a band change the firmware waits **3.5 s** before accepting measurements.

## 3. Optional GPS PPS

```text
GPS module             TTGO T-Display
--------------------------------------
PPS / 1PPS          -> GPIO33
GND                 -> GND
```

Set `PPS_ENABLED = true` in `include/config.h`.

## 4. Optional analog envelope

If a custom receiver front-end provides a safe 0-3.3 V envelope/RSSI diagnostic output:

```text
RX envelope          -> GPIO32
RX GND               -> GND
```

Set `DCF77_ANALOG_ENABLED = true`. The display shows the raw 12-bit ADC value.
