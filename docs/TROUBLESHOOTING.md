# Troubleshooting RAW DCF77

La build corrente serve prima di tutto a verificare il segnale elettrico del DCF-3850N-800.

## Collegamento minimo

```text
G  -> GND
V  -> 3.3 V
T  -> D7 / GPIO13
P1 -> D1 / GPIO5
```

Il firmware forza P1 LOW.

## Primo test

Aprire la console RAW e verificare:

- `DATA` cambia stato;
- `Edge totali` aumenta;
- `Edge / secondo` non resta sempre a 0;
- compaiono impulsi nella tabella.

## Se Edge totali = 0

Controllare nell'ordine:

1. P1 realmente vicino a 0 V;
2. V realmente presente sul pin V;
3. massa G comune;
4. T collegato al D7/GPIO13;
5. pinout fisico del modulo;
6. antenna lontana da ESP8266, OLED e alimentatori switching.

## Se ci sono edge ma impulsi strani

Prima non cambiare decoder: annotare livello idle, larghezza HIGH e periodo.

Il comportamento atteso è circa:

```text
100 ms
200 ms
1000 ms
2000 ms al marker
```

## INPUT_PULLUP

La baseline non abilita il pull-up ESP8266 sul pin T. Se il modulo produce edge solo con un pull-up esterno, va verificato elettricamente prima di modificare il firmware.

## Decoder

BCD, parità e accumulo non sono oggetto di questa fase. Verranno riattivati solo dopo avere confermato una forma d'onda RAW ripetibile.
