# DCF77: protocollo e recovery

## Segnale

DCF77 trasmette da Mainflingen a **77.5 kHz**. La codifica AM usa una riduzione di portante di circa **100 ms per 0** e **200 ms per 1**. Il marker del minuto è ottenuto omettendo il marker del secondo 59. La PTB specifica che ora/data trasmesse nel minuto si riferiscono al minuto successivo.

Riferimento: PTB, *About Time* / DCF77.

## Campi usati dal firmware

La 2.5.4 usa la struttura standard per la time information:

- bit 20: start time information, fisso a 1;
- bit 21..27 + P1(bit 28): minuti;
- bit 29..34 + P2(bit 35): ore;
- bit 36..57 + P3(bit 58): data;
- bit 16/17/18/19: annunci / CET-CEST / leap second quando disponibili.

## Erasure recovery

Il decoder distingue tra **bit errato** e **bit non osservato**. La recovery riguarda gli erasure (missing).

Per ogni blocco di parità:

1. se non manca alcun bit → verifica parità normale;
2. se manca esattamente un bit, incluso il bit di parità → il valore è ricostruibile univocamente;
3. se mancano due o più bit nello stesso blocco → il blocco non viene inventato e il frame non viene accettato.

In aggiunta, il bit 20 può essere ricostruito a 1 perché è fisso nello schema standard.

### Esempio

Un frame può avere:

```text
57 bit fisici
+ 1 missing nel blocco minuti
+ 1 missing nel blocco data
= 2 bit ricostruiti in modo deterministico
```

Se parità, BCD e range risultano validi, ora/data vengono salvate.

## Validazione finale

Il firmware richiede:

- struttura bit 20 corretta;
- P1 e P2 valide per salvare l'ora;
- minuti < 60, ore < 24;
- P3 valida per salvare la data;
- giorno 1..31 e mese 1..12.

## Secondi

Non esiste un campo numerico `secondi=xx` nel telegramma DCF77. Dopo il sync il firmware pone il riferimento al minuto valido e incrementa i secondi localmente con `millis()`.

## Limiti della recovery

La parità non è un codice ECC generale. Non consente di correggere in modo sicuro due unknown nello stesso blocco, né di sapere quale bit è errato se tutti risultano presenti ma uno è stato decodificato male.
