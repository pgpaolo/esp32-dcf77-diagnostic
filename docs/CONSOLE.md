# Console web DCF77

La console web mostra sia il segnale fisico ricevuto sia lo stato logico della decodifica.

![Anteprima console](console-preview.svg)

## Modalità decoder

Il selettore in alto permette di scegliere:

### DIRETTA / strict

Pensata per una ricezione molto pulita.

- 60..145 ms = bit 0;
- 155..245 ms = bit 1;
- qualunque impulso fuori finestra resta non valido;
- sono necessari tutti i 59 bit;
- P1, P2 e P3 devono essere corrette;
- un frame valido può sincronizzare direttamente l'orologio.

### ACCUMULO / radio clock

È la modalità predefinita e imita il comportamento di un orologio radiocontrollato robusto.

- ogni impulso viene confrontato con i nominali 100 e 200 ms;
- viene assegnata una **confidenza 0..100%** al bit;
- un impulso ambiguo può diventare `?` invece di far slittare il frame;
- i campi minuti, ore, data, mese e anno vengono valutati come candidati BCD pesati;
- i bit fortemente contraddittori fanno scartare il candidato;
- bit incerti possono essere recuperati dal candidato BCD più coerente;
- il primo minuto plausibile resta solo un candidato;
- la sincronizzazione avviene dopo almeno **2 minuti consecutivi coerenti**.

Questo evita il difetto di un accumulo ingenuo: non si somma lo stesso bit fisico per minuti diversi, perché i bit di minuti e ore naturalmente cambiano. Si accumulano invece **evidenza, confidenza dei campi e continuità temporale**.

La modalità selezionata viene salvata in EEPROM.

## Griglia dei 59 bit

La console visualizza le posizioni 0..58. Ogni cella mostra:

- posizione del secondo;
- valore `0`, `1` oppure `?`;
- confidenza percentuale;
- colore del gruppo DCF77.

Gruppi grafici:

| Posizioni | Gruppo |
|---|---|
| 0-15 | servizio |
| 16-20 | controllo / CET-CEST / start |
| 21-28 | minuti + P1 |
| 29-35 | ore + P2 |
| 36-58 | data + P3 |

La barra sopra la griglia indica l'avanzamento del frame corrente.

## Indicatori di accumulo

| Campo | Significato |
|---|---|
| Candidati coerenti | minuti consecutivi che rispettano la previsione +60 s |
| Confidenza campi | separazione complessiva fra i valori BCD migliori e le alternative |
| Bit incerti | bit assenti, ambigui o con confidenza bassa nel frame corrente |
| Bit recuperati | bit ricostruiti dal candidato BCD senza contraddire bit ricevuti con alta confidenza |

Quando `Candidati coerenti >= 2`, la modalità ACCUMULO può promuovere l'ora a sincronizzata.

## Campi principali

| Campo | Significato |
|---|---|
| Ora / Data | disponibili dopo sincronizzazione |
| Stato minuto | `SEARCH` prima del marker, `SYNC` dopo il marker |
| Qualità | media della confidenza degli impulsi recenti |
| Posizione frame | bit acquisiti nel minuto corrente |
| Impulso | ~100 ms = 0, ~200 ms = 1 |
| Periodo | ~1000 ms normale, ~2000 ms al marker |
| Marker minuto | numero di gap ~2 s riconosciuti |
| Frame validi / invalidi | esito dei frame completati |
| Parità KO | errori P1/P2/P3 |
| Timing KO | periodi incompatibili con 1 s / 2 s |

## Interpretazione rapida

Ricezione buona:

```text
Impulso: 101 ms
Periodo: 999 ms
Bit: 0
Confidenza: 99%
```

In ACCUMULO, durante l'acquisizione:

```text
Frame: 43 / 59
Candidati coerenti: 1
Confidenza campi: 78%
Bit incerti: 2
Bit recuperati: 1
```

Dopo il minuto successivo coerente:

```text
Candidati coerenti: 2
Clock: LOCK
```

## Diagnostica avanzata

La pagina principale include anche una sezione diagnostica sintetica. Il badge **STATO BUONO / ATTENZIONE / CRITICO** viene calcolato usando sincronizzazione minuto, qualità e percentuale di impulsi validi.

Sono mostrati:

- uptime;
- memoria heap libera;
- RSSI Wi-Fi quando connesso in STA;
- jitter RMS degli intervalli DCF77;
- percentuale complessiva di impulsi validi;
- conteggio glitch;
- età dell'ultimo frame valido;
- stato del clock lock;
- stato delle parità P1/P2/P3;
- distribuzione degli ultimi eventi fra `0`, `1` e `?`.

Questi indicatori permettono di distinguere rapidamente un problema RF/timing da un problema di frame o di parità.

## Configurazione OLED dal web

Il portale consente di salvare in EEPROM una delle seguenti viste:

- **AUTO**: rotazione automatica;
- **ORA**: ora/data;
- **SEGNALE**: misure del segnale DCF77;
- **DECODER**: frame e accumulo;
- **DIAGNOSTICA**: stato tecnico del ricevitore.

La selezione viene applicata senza ricompilare il firmware.
