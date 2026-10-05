# DCF77 HW-364A

Firmware dedicato a **ESP8266 NodeMCU HW-364A + OLED SSD1306 + ricevitore DCF77 77,5 kHz**.

Il progetto è stato riportato a una architettura semplice e specifica per una vera ricevente DCF77: acquisizione diretta degli edge digitali, decodifica 100/200 ms, marker minuto, parità e ora/data.

![Anteprima console DCF77](docs/console-preview.svg)

## Profilo PlatformIO predefinito

Il profilo **HW364A è il default del progetto**:

```ini
[platformio]
default_envs = hw364a
```

Quindi per compilare e caricare non è necessario specificare `-e hw364a`:

```bash
pio run
pio run -t upload
pio device monitor
```

Il comando esplicito resta comunque valido:

```bash
pio run -e hw364a
```

## Collegamenti

| Funzione | HW-364A |
|---|---:|
| DATA ricevitore DCF77 | D7 / GPIO13 |
| OLED SDA | D5 / GPIO14 |
| OLED SCL | D6 / GPIO12 |
| Pulsante pagina | FLASH / GPIO0 |
| GND | GND |

La configurazione predefinita considera DATA **active-low con pull-up interno**. Se il modulo acquistato fornisce un'uscita attiva alta, impostare `DCF77_ACTIVE_LOW=0` in `platformio.ini`.

> Verificare sempre tensione di alimentazione e pinout del modulo utilizzato: i ricevitori DCF77 commerciali non hanno tutti la stessa disposizione dei pin.

## Come viene codificato DCF77

DCF77 trasmette su **77,5 kHz**. Ogni secondo, eccetto il secondo 59, la portante viene attenuata all'inizio del secondo:

- attenuazione di circa **100 ms** = bit `0`;
- attenuazione di circa **200 ms** = bit `1`;
- al **secondo 59** l'impulso AM viene omesso: il ricevitore osserva quindi un intervallo di circa 2 s tra gli inizi degli impulsi del secondo 58 e del secondo 0 successivo.

Il frame contiene 59 bit utili. I campi principali sono:

- secondo 16: annuncio cambio ora legale;
- 17-18: CET/CEST;
- 19: annuncio secondo intercalare;
- 20: start-of-time-information;
- 21-27: minuti BCD;
- 28: parità minuti;
- 29-34: ore BCD;
- 35: parità ore;
- 36-58: data, giorno settimana, mese, anno e parità data.

La documentazione dettagliata è in [docs/DCF77_FRAME.md](docs/DCF77_FRAME.md).

## Funzionamento del firmware

Il firmware misura direttamente gli edge dell'uscita digitale del ricevitore:

1. rileva l'inizio dell'impulso;
2. misura la larghezza;
3. classifica 100 ms come `0` e 200 ms come `1`;
4. misura l'intervallo tra gli inizi degli impulsi;
5. riconosce il marker minuto quando l'intervallo è vicino a 2 s;
6. raccoglie 59 bit;
7. verifica struttura, BCD, parità P1/P2/P3 e plausibilità della data/ora;
8. solo un frame valido sincronizza l'orologio locale.

## Console web

All'avvio resta disponibile l'AP:

`DCF77-HW364A-xxxxxx`

con portale su `http://192.168.4.1`.

La console mostra:

- ora e data sincronizzate;
- stato SEARCH/SYNC;
- qualità del segnale;
- posizione del frame;
- ultimo impulso e periodo;
- marker minuto;
- frame validi/invalidi;
- errori di parità e timing;
- tabella degli impulsi recenti;
- configurazione Wi-Fi.

## Struttura essenziale

- `src/main.cpp`: acquisizione interrupt;
- `src/dcf77_decoder.cpp`: decodifica DCF77;
- `src/ui_oled.cpp`: OLED;
- `src/web_portal.cpp`: console web;
- `include/config.h`: pin, polarità e finestre temporali.

Vedi anche [docs/HW364A.md](docs/HW364A.md) e [docs/DCF77_FRAME.md](docs/DCF77_FRAME.md).
