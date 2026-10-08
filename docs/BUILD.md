# Build e configurazione

## Requisiti

- VS Code + PlatformIO oppure PlatformIO Core.
- Toolchain ESP8266 gestita da PlatformIO.
- Cavo USB dati e driver della board HW-364A, se necessario.

## Compilazione

```bash
pio run
```

Upload:

```bash
pio run -t upload
```

Monitor seriale:

```bash
pio device monitor -b 115200
```

L'ambiente predefinito è `hw364a_diag_web`.

## Wi-Fi senza segreti nel repository

Il progetto non contiene credenziali reali. Ci sono due metodi:

### Metodo raccomandato: captive portal

Lasciare `WIFI_SSID`/`WIFI_PASSWORD` vuoti. Dopo il timeout STA il firmware apre l'AP di setup e salva la configurazione in LittleFS.

### Fallback compilato

Creare localmente `include/secrets.h` con le macro `WIFI_SSID` e `WIFI_PASSWORD`. Il file è in `.gitignore` e non deve essere committato.

## Parametri PlatformIO

La configurazione pubblicata imposta:

- `DCF_DATA_PIN=13`
- `DCF_PON_PIN=5`
- `DCF_PON_ACTIVE_LOW=1`
- `DCF_IDLE_LOW=1`
- `OLED_ENABLED=1`
- `OLED_SDA_PIN=14`
- `OLED_SCL_PIN=12`
- `OLED_I2C_ADDR=0x3C`

Il probe OLED runtime prova comunque anche SDA/SCL invertiti e 0x3D.

## Prima accensione

1. Collegare RC8000.
2. Accendere la board.
3. Durante la ricerca l'OLED deve rimanere spento: è intenzionale.
4. Configurare Wi-Fi se necessario.
5. Attendere lock 1 Hz e frame validi.
6. Dopo N sync validi il ricevitore va OFF e l'OLED mostra l'ora in HOLDOVER.
