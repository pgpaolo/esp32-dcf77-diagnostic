# DCF77 HW-364A

Firmware dedicato a **ESP8266 NodeMCU HW-364A + OLED SSD1306 + ricevitore DCF77 77,5 kHz**.

Il progetto è stato semplificato per usare una vera ricevente DCF77: niente più modalità 60 kHz/MSF, dual-band, PLL software, campionamento RAW a 1 kHz o controlli specifici della vecchia ricevente.

## Collegamenti

| Funzione | HW-364A |
|---|---:|
| DATA ricevitore DCF77 | D7 / GPIO13 |
| OLED SDA | D5 / GPIO14 |
| OLED SCL | D6 / GPIO12 |
| Pulsante pagina | FLASH / GPIO0 |
| GND | GND |

La configurazione predefinita considera DATA **active-low con pull-up interno**. Se il modulo acquistato fornisce un'uscita attiva alta, impostare `DCF77_ACTIVE_LOW=0` in `platformio.ini`.

> Verificare sempre VCC e pinout del proprio modulo DCF77 prima del collegamento: i moduli in commercio non hanno tutti la stessa tensione o disposizione dei pin.

## Funzionamento

Il firmware misura direttamente gli edge dell'uscita digitale:

- circa 100 ms = bit 0;
- circa 200 ms = bit 1;
- intervallo di circa 2 s tra gli inizi di due impulsi = marker del minuto, dovuto all'assenza dell'impulso al secondo 59.

Dopo il marker vengono acquisiti i 59 bit del nuovo frame. Il frame viene accettato solo se struttura, parità P1/P2/P3 e data/ora sono coerenti. Un frame valido sincronizza l'orologio locale.

## Portale

All'avvio resta disponibile l'AP:

`DCF77-HW364A-xxxxxx`

con portale su `http://192.168.4.1`. Mostra ora, qualità, impulsi recenti, frame validi/invalidi e permette di configurare la rete Wi-Fi.

## Compilazione

```bash
pio run -e hw364a
pio run -e hw364a -t upload
```

## Struttura essenziale

- `src/main.cpp`: acquisizione interrupt;
- `src/dcf77_decoder.cpp`: decodifica DCF77;
- `src/ui_oled.cpp`: OLED;
- `src/web_portal.cpp`: portale locale;
- `include/config.h`: pin e soglie.

Vedi anche `docs/HW364A.md` e `docs/DCF77_FRAME.md`.
