# Dual-frequency 60 / 77.5 kHz support

## Supported hardware profile

The dedicated dual-band build targets receivers compatible with the **C-MAX CMMR-6D-7760 / CME6005** architecture.

Published C-MAX data for the EU dual-frequency version includes:

- carrier frequencies: 60 / 77.5 kHz
- BAND low/GND: 77.5 kHz
- BAND high/VDD: 60 kHz
- TCO positive data output
- TCON inverted data output
- active-low PON
- HLD AGC hold input
- typical current at 3 V: 150 uA
- startup time: <3.5 s
- receiver sensitivity via specified 60 mm antenna: <30 uV/m
- antenna inductance stated for the dual version: 3.9 mH
- output pulse-width tolerance: < +/-30 ms

Source: C-MAX CMMR-6 receiver-module specification.

## Why 60 kHz is RAW mode

A carrier frequency does not identify one universal time-code format. At 60 kHz a receiver may encounter MSF, WWVB or JJY60, depending on location. Those services do not use the same AM bit coding as DCF77.

Therefore the firmware deliberately does **not** decode 60-kHz pulses as DCF77 bits.

The 60-kHz mode currently measures:

- pulse width
- one-second period
- instantaneous jitter
- RMS jitter
- malformed/missing timing
- glitch count
- rolling signal/timing quality
- optional PPS offset

## Future protocol layer

The receiver-control and timing layers are separated from the protocol decoder:

```text
RF receiver
   |
edge capture
   |
timing analyzer
   +---- DCF77 decoder (77.5 kHz)
   +---- MSF decoder   (60 kHz)
   +---- WWVB decoder  (60 kHz)
   +---- JJY60 decoder (60 kHz)
```

Automatic protocol detection should only be added after enough valid pulse history has been collected; frequency alone is not sufficient to distinguish MSF, WWVB and JJY60.
