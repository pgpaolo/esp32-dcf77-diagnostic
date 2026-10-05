# Console web DCF77

La console web è pensata come strumento operativo, non come oscilloscopio. Con una ricevente DCF77 corretta devono bastare pochi indicatori per capire lo stato del sistema.

![Anteprima console](console-preview.svg)

## Campi principali

| Campo | Significato |
|---|---|
| Ora / Data | disponibili solo dopo un frame DCF77 valido |
| Stato minuto | `SEARCH` prima del marker, `SYNC` dopo il riconoscimento del minuto |
| Qualità | percentuale calcolata sugli impulsi recenti |
| Posizione frame | numero di bit acquisiti nel minuto corrente |
| Impulso | durata dell'ultimo impulso: ~100 ms = 0, ~200 ms = 1 |
| Periodo | distanza tra gli inizi di due impulsi |
| Marker minuto | numero di intervalli ~2 s riconosciuti |
| Frame validi | frame completi con struttura e parità corrette |
| Frame invalidi | frame completi ma non accettabili |
| Parità KO | errori P1/P2/P3 |
| Timing KO | periodi incompatibili con 1 s o 2 s |

## Interpretazione rapida

Ricezione buona:

```text
Impulso: 101 ms
Periodo: 999 ms
Bit: 0
Qualità: 95%
```

oppure:

```text
Impulso: 198 ms
Periodo: 1001 ms
Bit: 1
Qualità: 96%
```

Al cambio minuto:

```text
Periodo: ~2000 ms
Stato minuto: SYNC
Posizione frame: 1
```

## Tabella impulsi recenti

La tabella conserva gli ultimi eventi con:

- età;
- durata impulso;
- periodo;
- bit;
- stato OK/KO/MIN.

Questo rende immediato distinguere un problema RF da un problema di decodifica.
