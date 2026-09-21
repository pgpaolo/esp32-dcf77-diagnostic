# Wiring

## Minimal DCF77 setup

```text
                         TTGO T-Display
                      +------------------+
DCF77 DATA/OUT --------| GPIO27           |
DCF77 GND -------------| GND              |
                      |                  |
                      | ST7789 integrated|
                      +------------------+
```

Power the DCF77 module according to its own datasheet.

## Optional GPS PPS

```text
GPS module             TTGO T-Display
--------------------------------------
PPS / 1PPS          -> GPIO33
GND                 -> GND
```

Set in `include/config.h`:

```cpp
constexpr bool PPS_ENABLED = true;
```

The firmware reports the nearest signed phase difference between the DCF77 second edge and GPS PPS in microseconds, normalized to +/-500 ms.

## Optional analog envelope

If a custom active receiver provides a safe 0-3.3 V envelope/RSSI-like diagnostic output:

```text
RX envelope          -> GPIO32
RX GND               -> GND
```

Then set:

```cpp
constexpr bool DCF77_ANALOG_ENABLED = true;
```

The default firmware displays the raw 12-bit ADC value. Calibration to voltage, dB or relative field strength must be done for the specific receiver front-end.
