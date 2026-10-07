# API web

Il firmware espone una piccola API HTTP sul portale locale.

Base AP:

```text
http://192.168.4.1
```

## GET /api/status

Restituisce lo stato generale.

Campi principali:

| Campo | Descrizione |
|---|---|
| `clockAvailable` | clock DCF77 disponibile |
| `time` | ora HH:MM:SS |
| `date` | data e CET/CEST |
| `mode` | `direct` / `accumulate` |
| `displayMode` | vista OLED |
| `minuteSynced` | marker minuto acquisito |
| `clockLocked` | orologio sincronizzato |
| `quality` | qualità media impulsi |
| `frameBitCount` | posizione nel frame |
| `pulseMs` | durata ultimo impulso |
| `periodMs` | periodo ultimo impulso |
| `validFrames` | frame validi |
| `invalidFrames` | frame non validi |
| `candidateMinutes` | candidati coerenti in ACCUMULO |
| `fieldConfidence` | confidenza campi BCD |
| `uncertainBits` | bit incerti |
| `recoveredBits` | bit recuperati |
| `jitterRmsMs` | RMS jitter |
| `validRatio` | percentuale impulsi validi |
| `freeHeap` | heap libero |
| `rssi` | RSSI Wi-Fi STA |
| `p1/p2/p3` | stato parità |

## GET /api/frame

Restituisce:

- frame corrente;
- ultimo frame completato;
- array dei 59 bit;
- confidenza per ogni bit.

Esempio semplificato:

```json
{
  "current": {
    "count": 23,
    "bits": [0,1,0,-1],
    "confidence": [100,94,88,0]
  }
}
```

`-1` indica bit non disponibile/incerto.

## GET /api/pulses

Restituisce gli ultimi impulsi osservati con:

- età;
- larghezza;
- periodo;
- bit;
- confidenza;
- validità;
- marker minuto.

## POST /api/mode

Parametri form-urlencoded:

```text
mode=direct
```

oppure:

```text
mode=accumulate
```

Il cambio modalità azzera il decoder.

## POST /api/display

Valori:

```text
auto
clock
signal
decoder
diagnostics
```

La scelta viene salvata in EEPROM.

## GET /api/networks

Scansione reti Wi-Fi.

## POST /api/wifi/connect

Parametri:

```text
ssid=<nome>
password=<password>
```

## GET /api/wifi

Restituisce stato STA, SSID e IP.

## POST /api/reset

Azzera lo stato del decoder senza riavviare ESP8266.

## Nota di sicurezza

Il portale è pensato per uso locale/embedded.

L'AP non implementa autenticazione applicativa. Se il dispositivo viene usato in un ambiente non fidato è consigliabile:

- proteggere l'AP;
- limitare l'accesso di rete;
- non esporre il portale direttamente su Internet.


## POST /api/receiver/power

Controlla il pin hardware PON del ricevitore.

Parametri form-urlencoded:

```text
action=on
action=off
action=restart
```

Comportamento:

- `on`: abilita il ricevitore;
- `off`: disabilita il ricevitore;
- `restart`: PON OFF per 3 secondi, reset decoder, quindi PON ON.

`GET /api/status` espone anche:

| Campo | Descrizione |
|---|---|
| `receiverEnabled` | stato hardware richiesto del ricevitore |
| `receiverRestarting` | riavvio PON in corso |
