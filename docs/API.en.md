# Web interface and API

The ESP8266 serves the Web UI directly on port 80. The console polls `/api/status`.

## Main endpoints

| Method | Endpoint | Purpose |
|---|---|---|
| GET | `/` | DCF77 console |
| GET | `/api/status` | full diagnostic JSON |
| GET | `/wifi` | Wi-Fi configuration page |
| GET | `/api/wifi/scan` | on-demand network scan |
| POST | `/api/wifi/save` | save SSID/password/hostname and reboot |
| POST | `/api/wifi/reset` | remove `/wifi.cfg` |
| GET | `/api/quiet/start` | 60 s RF QUIET test |
| GET | `/api/ab/start` | AUTO A/B: INPUT 60 s + INPUT_PULLUP 60 s |
| POST | `/api/radio/save` | receiver duty timer settings |
| GET | `/action?do=...` | internal/legacy diagnostic endpoint |

## `/api/status`

The status object includes DATA/PON/receiver state, radio timer/countdown, RAW edge and pulse statistics, PLL/phase/slot diagnostics, marker/lost-bit information, frame and recovery counters, decoded clock/date/timezone data, Wi-Fi state, OLED/I2C state, AUTO A/B results and RF QUIET results.

It is a diagnostic payload rather than a formally versioned public API; external clients should tolerate added fields.

## Radio timer

`POST /api/radio/save` accepts:

- `enabled=0|1`
- `syncs=1..10`
- `minutes=1..1440`

The v2.5.4 UI exposes only these timer controls. OLED behavior is owned by the same state machine and has no normal manual button.

## `/action` endpoint

This endpoint remains for internal diagnostics (receiver on/off/reset, DATA bias, counter clear, raw logging). It is not exposed as a normal v2.5.4 UI control and manual use can interfere with duty-cycle behavior.
