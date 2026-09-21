# DCF77 frame fields used by the decoder

The decoder treats each received 100/200 ms amplitude-reduction mark as the bit associated with that second number.

| Seconds | Meaning |
|---|---|
| 0 | minute marker bit M, normally 0 |
| 1-14 | service/weather information, not decoded |
| 15 | call bit / service bit |
| 16 | A1: daylight-saving transition announcement |
| 17 | Z1 |
| 18 | Z2 |
| 19 | A2: leap-second announcement |
| 20 | S: start of encoded time, must be 1 |
| 21-27 | minute BCD |
| 28 | P1 minute parity |
| 29-34 | hour BCD |
| 35 | P2 hour parity |
| 36-41 | day BCD |
| 42-44 | weekday (Monday=1) |
| 45-49 | month BCD |
| 50-57 | year BCD |
| 58 | P3 date parity |
| 59 | no normal AM second mark; identifies the minute transition |

Timezone interpretation:

- Z1=0, Z2=1 -> CET
- Z1=1, Z2=0 -> CEST

P1, P2 and P3 are even parity checks.
