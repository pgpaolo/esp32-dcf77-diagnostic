# Contributing

Contributi, test hardware e segnalazioni sono benvenuti.

## Build locale

Requisiti:

- Python;
- PlatformIO Core;
- ESP8266 NodeMCU / HW-364A.

Il target predefinito è già `hw364a`:

```bash
pio run
```

Upload:

```bash
pio run -t upload
```

Monitor seriale:

```bash
pio device monitor
```

## Prima di aprire una pull request

Verificare che:

1. `pio run` termini con successo;
2. non vengano modificati pin o polarità senza aggiornare `docs/HW364A.md`;
3. qualunque modifica al decoder sia accompagnata da una spiegazione tecnica;
4. modifiche all'API siano riportate in `docs/API.md`;
5. modifiche visibili nella console siano riportate in `docs/CONSOLE.md`;
6. il changelog venga aggiornato per modifiche funzionali.

## Stile

- C++ compatibile con Arduino ESP8266;
- evitare allocazioni dinamiche ripetitive nel percorso di acquisizione;
- nessuna operazione lenta dentro la ISR;
- mantenere separati acquisizione, decoder, UI e portale web;
- preferire costanti esplicite per le soglie temporali;
- evitare dipendenze non necessarie.

## Segnalazioni RF / ricezione

Per problemi di ricezione allegare:

- modello ricevitore;
- tensione di alimentazione;
- polarità DATA;
- `DCF77_ACTIVE_LOW`;
- larghezza impulsi osservata;
- periodo osservato;
- jitter RMS;
- percentuale impulsi validi;
- screenshot della diagnostica web;
- eventuale foto/schema del cablaggio.

## Pull request

Descrivere:

- problema risolto;
- comportamento precedente;
- comportamento nuovo;
- hardware usato per il test;
- risultato di `pio run`.
