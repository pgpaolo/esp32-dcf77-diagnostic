# Codifica del segnale DCF77

DCF77 è il servizio tedesco di distribuzione dell'ora legale trasmesso da Mainflingen su **77,5 kHz**. Il firmware di questo progetto utilizza la modulazione di ampiezza ricevuta dal modulo DCF77 e la trasforma in bit temporali.

## Codifica fisica dei bit

All'inizio di quasi ogni secondo la portante viene attenuata per una durata che identifica il bit:

| Durata impulso | Significato |
|---:|---|
| circa 100 ms | bit `0` |
| circa 200 ms | bit `1` |
| nessun impulso al secondo 59 | marker del nuovo minuto |

Di conseguenza, durante un minuto normale si osservano 59 impulsi. Tra l'inizio dell'impulso del secondo 58 e l'inizio dell'impulso del secondo 0 del minuto successivo trascorrono circa **2 secondi**.

Nel firmware vengono usate finestre temporali tolleranti:

- `60..145 ms` -> bit 0;
- `155..245 ms` -> bit 1;
- `1700..2300 ms` tra due fronti iniziali -> marker minuto.

La zona tra 145 e 155 ms viene lasciata volutamente non classificata.

## Struttura dei 60 secondi

| Secondo | Contenuto |
|---:|---|
| 0 | bit M, normalmente 0 |
| 1-14 | dati di servizio |
| 15 | bit di servizio/call bit |
| 16 | A1: annuncio cambio CET/CEST |
| 17 | Z1 |
| 18 | Z2 |
| 19 | A2: annuncio secondo intercalare |
| 20 | S: inizio informazione oraria, deve essere 1 |
| 21-27 | minuti in BCD |
| 28 | P1: parità minuti |
| 29-34 | ore in BCD |
| 35 | P2: parità ore |
| 36-41 | giorno del mese in BCD |
| 42-44 | giorno della settimana, lunedì=1 |
| 45-49 | mese in BCD |
| 50-57 | anno a due cifre in BCD |
| 58 | P3: parità data |
| 59 | nessun impulso AM normale: marker di minuto |

## CET e CEST

I bit 17 e 18 sono complementari:

| Z1 | Z2 | Zona |
|---:|---:|---|
| 0 | 1 | CET |
| 1 | 0 | CEST |

Una combinazione `00` o `11` viene considerata incoerente.

## Codifica BCD

I valori numerici sono trasmessi in **Binary Coded Decimal**. Esempio: minuto 37.

Unità 7:

```text
1 + 2 + 4 = 7
```

Decine 3:

```text
10 + 20 = 30
```

Quindi il decoder somma i pesi dei bit attivi nelle posizioni previste.

### Minuti

| Secondo | Peso |
|---:|---:|
| 21 | 1 |
| 22 | 2 |
| 23 | 4 |
| 24 | 8 |
| 25 | 10 |
| 26 | 20 |
| 27 | 40 |

### Ore

| Secondo | Peso |
|---:|---:|
| 29 | 1 |
| 30 | 2 |
| 31 | 4 |
| 32 | 8 |
| 33 | 10 |
| 34 | 20 |

La stessa logica viene applicata a giorno, mese e anno.

## Parità

DCF77 utilizza parità pari:

- **P1** al secondo 28 protegge i minuti 21-27;
- **P2** al secondo 35 protegge le ore 29-34;
- **P3** al secondo 58 protegge data, giorno settimana, mese e anno 36-57.

Il numero totale di bit a 1 nel gruppo, includendo il bit di parità, deve essere pari.

## Sequenza di sincronizzazione usata dal firmware

Il firmware non deve conoscere a priori il secondo assoluto. Procede così:

```text
... sec 57 -> impulso 0/1
    sec 58 -> impulso 0/1
    sec 59 -> nessun impulso
    sec 00 -> impulso, periodo misurato circa 2000 ms
               ^
               marker minuto rilevato
```

Il primo impulso dopo il gap è quindi il bit del **secondo 0**. Da quel momento il decoder riempie ordinatamente le posizioni 0..58 del frame.

## Criteri di validità

Un frame viene accettato solo se:

- sono disponibili tutti i 59 bit;
- il bit S al secondo 20 vale 1;
- Z1/Z2 sono coerenti;
- P1, P2 e P3 sono corrette;
- minuti 0..59 e ore 0..23;
- mese 1..12;
- giorno compatibile con mese e anno bisestile;
- giorno settimana 1..7.

Un frame incompleto o incoerente non aggiorna l'orologio.
