<!--
DCF77 RC8000 Console v2.5.4
Copyright (c) 2026 Gianpaolo P.
Licensed under the PolyForm Noncommercial License 1.0.0.
Commercial use requires a separate written license from the copyright holder.
See LICENSE and NOTICE.md.
-->

# Wiring

## RC8000 ↔ HW-364A connections

```text
RC8000 / DCF-3850N-800          HW-364A / ESP8266
--------------------------------------------------
VDD / 3.3 V              --->   3V3
GND                      --->   GND
DATA                     --->   GPIO13
PON                      --->   GPIO5
```

`PON` is configured **active LOW**:

- GPIO5 = LOW → receiver ON;
- GPIO5 = HIGH → receiver OFF.

The OLED is integrated on the HW-364A and requires no external wiring. The firmware uses GPIO14/GPIO12 as the primary mapping and probes the reverse order as well.

## Practical notes

- Verify 3.3 V at the receiver itself.
- Use a common ground.
- Keep DATA/PON wiring short and away from the ferrite antenna when possible.
- Avoid routing USB/switching-power wiring directly over the antenna.
- If DATA produces excessive spurious edges, run AUTO A/B before changing hardware.
