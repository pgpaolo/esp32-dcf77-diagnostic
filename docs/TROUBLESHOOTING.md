# Collaudo e troubleshooting

## 1. Verifica build

Il progetto usa HW364A come target predefinito:

```bash
pio run
```

Upload:

```bash
pio run -t upload
```

Monitor:

```bash
pio device monitor
```

## 2. Primo controllo elettrico

Verificare:

- VCC corretto per il modulo DCF77;
- massa comune;
- DATA su D7 / GPIO13;
- polarità dell'uscita;
- necessità o meno del pull-up interno.

Default:

```ini
-D DCF77_ACTIVE_LOW=1
-D DCF77_USE_INTERNAL_PULLUP=1
```

## 3. Segnale atteso

Con una ricezione corretta:

```text
~100 ms  -> bit 0
~200 ms  -> bit 1
~1000 ms -> periodo normale
~2000 ms -> marker minuto
```

## 4. Se gli impulsi sono invertiti

Se le durate risultano chiaramente complementari o non plausibili, provare:

```ini
-D DCF77_ACTIVE_LOW=0
```

## 5. Se non arriva nulla

Controllare:

- pinout reale del ricevitore;
- alimentazione;
- DATA con multimetro/oscilloscopio;
- antenna in ferrite;
- orientamento antenna;
- distanza da ESP8266, USB, alimentatori switching, display e DC/DC.

## 6. Se arrivano 100/200 ms ma non compare SYNC

Attendere almeno un passaggio di minuto.

Il sincronismo del minuto viene rilevato solo quando compare il gap corrispondente al secondo 59.

Verificare:

```text
Periodo ~2000 ms
Marker minuto > 0
Stato minuto = SYNC
```

## 7. Se SYNC c'è ma il clock non si blocca

Controllare:

- posizione frame che salga fino a 59;
- parità P1/P2/P3;
- bit 20 = 1;
- CET/CEST coerenti;
- frame invalidi;
- bit incerti.

In modalità ACCUMULO sono richiesti due minuti coerenti.

## 8. Diagnostica web

Indicatori utili:

| Indicatore | Lettura buona |
|---|---|
| Qualità | idealmente > 75% |
| Impulsi validi | idealmente > 80% |
| Jitter RMS | basso e stabile |
| Timing KO | quasi fermo |
| Glitch | quasi fermo |
| P1/P2/P3 | OK dopo frame completo |
| Clock lock | SI |

Le soglie sono indicative: dipendono dal ricevitore e dall'ambiente RF.

## 9. Disturbi tipici

Fonti frequenti:

- alimentatori switching economici;
- USB 3.x;
- convertitori DC/DC;
- display;
- Wi-Fi molto vicino all'antenna;
- cavi lunghi non schermati;
- antenna ferrite parallela a una sorgente di rumore.

È utile allontanare fisicamente antenna e modulo RF dall'ESP8266.

## 10. Procedura di collaudo consigliata

1. verificare 10-20 impulsi 100/200 ms;
2. verificare periodo vicino a 1 s;
3. attendere marker minuto;
4. verificare avanzamento 0..58;
5. verificare P1/P2/P3;
6. in DIRETTA attendere un frame valido;
7. in ACCUMULO attendere due candidati coerenti;
8. verificare ora/data;
9. lasciare il sistema acceso almeno 30 minuti;
10. controllare jitter, errori e frame invalidi.

## 11. Reset software

Dal portale:

```text
Reset decoder
```

oppure:

```bash
POST /api/reset
```

Il reset non modifica le impostazioni EEPROM.
