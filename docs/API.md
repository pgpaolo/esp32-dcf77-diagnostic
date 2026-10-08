<!--
DCF77 RC8000 Console v2.5.4
Copyright (c) 2026 Gianpaolo P.
Licensed under the PolyForm Noncommercial License 1.0.0.
Commercial use requires a separate written license from the copyright holder.
See LICENSE and NOTICE.md.
-->

# Interfaccia Web e API

La UI Web è servita direttamente dall'ESP8266 sulla porta 80. Il polling della console usa `/api/status`.

## Endpoint principali

| Metodo | Endpoint | Uso |
|---|---|---|
| GET | `/` | console DCF77 |
| GET | `/api/status` | stato JSON completo |
| GET | `/wifi` | configurazione Wi-Fi |
| GET | `/api/wifi/scan` | scansione reti on-demand |
| POST | `/api/wifi/save` | salva SSID/password/hostname e riavvia |
| POST | `/api/wifi/reset` | cancella `/wifi.cfg` |
| GET | `/api/quiet/start` | test RF QUIET di 60 s |
| GET | `/api/ab/start` | AUTO A/B: INPUT 60 s + INPUT_PULLUP 60 s |
| POST | `/api/radio/save` | timer radio: enable/syncs/minutes |
| GET | `/action?do=...` | endpoint diagnostico interno/legacy |

## `/api/status`

Il JSON include, tra gli altri:

- stato DATA/PON/ricevitore;
- timer radio e countdown;
- RAW edge counters, bit0/bit1/noise e valid%;
- PLL, fase, slot invalidi, reject counters;
- marker osservati/inferiti e bit persi;
- frame OK/KO e risultati recovery;
- clock, data, timezone, weekday, DST/leap announcements;
- Wi-Fi, RSSI, IP, captive portal;
- OLED, driver, ACK I2C, pin rilevati e stato RF quiet;
- risultati AUTO A/B e RF QUIET.

La struttura è diagnostica e può evolvere; evitare di trattarla come API stabile senza versionamento nel proprio client.

## Timer radio

`POST /api/radio/save` accetta query/form args:

- `enabled=0|1`
- `syncs=1..10`
- `minutes=1..1440`

La UI espone soltanto questi controlli. Il display è gestito automaticamente dalla stessa state machine, non da pulsanti manuali.

## Endpoint `/action`

Resta nel firmware per diagnostica interna (receiver on/off/reset, bias, counters, rawlog). Non è esposto come controllo ordinario nella UI v2.5.4 e può interferire con il duty-cycle se usato manualmente.
