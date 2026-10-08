<!--
DCF77 RC8000 Console v2.5.4
Copyright (c) 2026 Gianpaolo P.
Licensed under the PolyForm Noncommercial License 1.0.0.
Commercial use requires a separate written license from the copyright holder.
See LICENSE and NOTICE.md.
-->

# Troubleshooting

## OLED is dark during SEARCH

This is **expected** in v2.5.4. The display stays RF-quiet while the DCF receiver is powered. It should turn on when the duty timer powers the receiver down after valid synchronization.

## OLED `NOT DETECTED`

The firmware probes GPIO14/GPIO12, the reversed GPIO12/GPIO14 order, addresses 0x3C/0x3D, SSD1306 and SH1106 fallback. If no I2C ACK is received, check board/display power and the actual hardware variant.

## 1 Hz CLOCK LOCK but frames fail

Inspect invalid slots, lost-bit gaps, missing bits before/after recovery, parity-block erasures, inferred markers and out-of-phase rejects. Stable timing lock does not guarantee that every useful bit is uniquely recoverable.

## Low quality / many spikes

Keep OLED off during RX (default), avoid Wi-Fi scans, run RF QUIET for comparison, use AUTO A/B to compare INPUT and INPUT_PULLUP, rotate/move the ferrite antenna, improve supply decoupling and increase distance from USB/switching electronics.

## Receiver produces no edges

Check PON: GPIO5 must be LOW when `receiverOn=true` with the reference active-low configuration. Verify 3.3 V and common ground at the RC8000.

## Frame with 2–3 missing bits

It can be recovered only when the erasures are spread across independent parity blocks with at most one unknown per block. Multiple unknowns inside one block are not uniquely solvable.

## Wi-Fi does not connect

Wait for fallback AP `DCF77-Setup-XXXXXX`, connect to it and open `192.168.4.1`.

## Timer does not power the receiver down

The configured number of **consecutive frames with both valid time and date** must be reached. A failed frame resets the consecutive counter.
