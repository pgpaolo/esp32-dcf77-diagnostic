# Architettura firmware

## Obiettivo

Il firmware è progettato per una configurazione precisa:

- ESP8266 NodeMCU / HW-364A;
- ricevitore DCF77 77,5 kHz con uscita digitale;
- OLED SSD1306 128x64;
- portale web locale per configurazione e diagnostica.

L'architettura evita ricostruzioni software della portante RF: il modulo DCF77 deve fornire un'uscita digitale già demodulata.

## Flusso dati

```text
Ricevitore DCF77
      |
      | DATA
      v
GPIO13 / D7
      |
      | interrupt CHANGE
      v
Acquisizione edge
      |
      | widthUs / periodUs
      v
DCF77Decoder
      |
      +--> DIRETTA / strict
      |
      +--> ACCUMULO / radio clock
      |
      +--> statistiche / frame / clock
             |
             +--> OLED
             +--> Web API
             +--> Console web
```

## Acquisizione degli impulsi

`src/main.cpp` misura:

- istante di inizio impulso;
- durata dell'impulso;
- periodo fra due inizi impulso consecutivi.

Gli eventi vengono inseriti in una piccola coda ISR-safe e processati nel loop principale.

Questo separa il timing critico dell'interrupt dalle attività più lente come Wi-Fi, web server e OLED.

## Decoder

Il decoder riceve una struttura:

```cpp
RawPulse {
    startUs
    widthUs
    periodUs
}
```

e produce:

- bit 0 / 1 / incerto;
- confidenza;
- stato del minuto;
- posizione del frame;
- parità;
- data/ora;
- statistiche diagnostiche.

### Modalità DIRETTA

Usa finestre rigide:

```text
60..145 ms   -> 0
155..245 ms  -> 1
```

Un frame è valido solo se completo e coerente.

### Modalità ACCUMULO

Usa una classificazione morbida centrata su 100 e 200 ms.

Ogni bit riceve una confidenza. I campi BCD vengono poi valutati scegliendo il candidato più coerente con i bit ricevuti.

L'orologio non viene promosso a sincronizzato al primo candidato: sono richiesti due minuti consecutivi temporalmente coerenti.

## Marker del minuto

DCF77 omette il normale impulso AM al secondo 59.

Il firmware riconosce quindi:

```text
periodo normale     ~1000 ms
passaggio 58 -> 00  ~2000 ms
```

Il primo impulso dopo il gap è il bit del secondo 0.

## Clock locale

Quando un frame è accettato, il decoder salva una base temporale DCF77.

Da quel momento l'ora viene fatta avanzare localmente usando `millis()` fino alla successiva sincronizzazione valida.

## Persistenza EEPROM

Sono persistenti:

- modalità decoder: DIRETTA / ACCUMULO;
- modalità OLED: AUTO / ORA / SEGNALE / DECODER / DIAGNOSTICA.

Le impostazioni vengono caricate all'avvio dal portale.

## Moduli principali

| File | Funzione |
|---|---|
| `src/main.cpp` | interrupt, coda impulsi, loop |
| `src/dcf77_decoder.cpp` | classificazione, frame, BCD, parità, clock |
| `src/ui_oled.cpp` | quattro viste OLED |
| `src/web_portal.cpp` | AP, API REST, console HTML |
| `include/config.h` | pin e soglie temporali |
| `platformio.ini` | target HW364A predefinito |

## Stato di acquisizione

Il comportamento previsto è:

```text
SEARCH
  |
  | primo marker minuto
  v
SYNC minuto
  |
  | acquisizione 59 bit
  v
frame candidato
  |
  +--> DIRETTA: valido -> CLOCK LOCK
  |
  +--> ACCUMULO: candidato 1
                   |
                   | minuto successivo coerente
                   v
                 CLOCK LOCK
```

## Robustezza

Il firmware non tenta di correggere un segnale elettrico gravemente errato.

La robustezza viene applicata a:

- tolleranza sulle durate;
- classificazione probabilistica in ACCUMULO;
- controllo della continuità temporale;
- BCD;
- parità;
- range data/ora;
- diagnostica di timing e qualità.
