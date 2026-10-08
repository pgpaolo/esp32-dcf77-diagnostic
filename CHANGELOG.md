# Changelog

## Licensing update — 2026-10-08

- Replaced the MIT license with **PolyForm Noncommercial License 1.0.0**.
- Added `NOTICE.md` with mandatory attribution notices and commercial-licensing guidance.
- Clarified in both Italian and English documentation that commercial use requires a separate written license.
- Firmware source and behavior remain unchanged.

## 2.5.4 — stable HW-364A / RC8000 baseline

- OLED ownership moved entirely into the radio duty-cycle state machine.
- RADIO ON / DCF77 search: OLED off and I2C display traffic suppressed.
- RADIO OFF / HOLDOVER: OLED automatically on, showing time/date.
- OLED is shut down before the receiver is powered up again.
- Normal manual OLED controls removed from the Web UI.
- Normal manual radio ON/OFF controls removed from the timer UI.
- Persistent timer controls kept: enable, valid-sync target, OFF duration.
- Robust v2.4 frame finalization and parity erasure recovery retained.
- HW-364A OLED auto-probe retained (GPIO14/12 and reversed order, 0x3C/0x3D, SSD1306/SH1106 fallback).

## Recovery lineage retained in 2.5.4

- Statistical 1 Hz phase recovery.
- Slot-indexed 59-bit frame handling.
- Minute marker inference.
- Fixed bit-20 reconstruction.
- One-erasure recovery per minute/hour/date parity block.
- Local HH:MM:SS holdover and periodic DCF re-synchronization.

This repository intentionally publishes the 2.5.4 source tree as the clean baseline; intermediate experimental source trees are not kept in the current branch contents.
