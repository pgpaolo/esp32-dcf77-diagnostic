<!--
DCF77 RC8000 Console v2.5.4
Copyright (c) 2026 Gianpaolo P.
Licensed under the PolyForm Noncommercial License 1.0.0.
Commercial use requires a separate written license from the copyright holder.
See LICENSE and NOTICE.md.
-->

# DCF77 protocol and recovery

## Signal

DCF77 transmits from Mainflingen on **77.5 kHz**. Its amplitude-modulated second markers use roughly **100 ms for binary 0** and **200 ms for binary 1**. The minute boundary is identified by omitting the second marker at second 59. PTB specifies that the transmitted time/date refer to the following minute.

Reference: PTB, *About Time* / DCF77.

## Fields used by the firmware

- bit 20: start of encoded time, fixed to 1;
- bits 21..27 + P1(bit 28): minutes;
- bits 29..34 + P2(bit 35): hours;
- bits 36..57 + P3(bit 58): date;
- bits 16/17/18/19: announcements / CET-CEST / leap-second information when observed.

## Erasure recovery

The decoder distinguishes a **wrong decoded bit** from a **missing observation**. Recovery applies to missing bits (erasures).

For each parity block:

1. no missing bit → normal parity check;
2. exactly one missing bit, including the parity bit → value is uniquely reconstructable;
3. two or more missing bits in the same block → the firmware refuses to guess.

Bit 20 can additionally be restored to 1 because it is fixed by the standard frame structure.

### Example

```text
57 physical bits
+ 1 erasure in the minute block
+ 1 erasure in the date block
= 2 deterministically reconstructed bits
```

If parity, BCD and range checks all pass, time/date are accepted.

## Final validation

The firmware requires a valid bit-20 structure, P1/P2 for time, minute < 60, hour < 24, P3 for date, day 1..31 and month 1..12.

## Seconds

DCF77 does not transmit a numeric seconds field. After synchronization the firmware maintains seconds locally using `millis()` and re-aligns on future DCF synchronization.

## Recovery limits

Parity is not a general ECC. It cannot safely recover two unknown bits in one parity block, nor identify which present bit is wrong when a complete block has a bit flip.
