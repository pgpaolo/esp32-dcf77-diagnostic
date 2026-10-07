# Baseline RAW DCF77

Questa versione del firmware non tenta ancora di decodificare data e ora.

Lo scopo è verificare prima il comportamento elettrico reale del modulo **DCF-3850N-800 / SP6007**.

## Pin del modulo

| Modulo | Funzione | HW-364A |
|---|---|---|
| G | Ground | GND |
| V | Alimentazione | 3.3 V max |
| T | Uscita dati | D7 / GPIO13 |
| P1 | Power On/Off | D1 / GPIO5, forzato LOW |

Il produttore indica che **P1 deve essere a livello logico LOW** per abilitare il ricevitore.

## Impostazione DATA

La baseline usa:

```cpp
pinMode(PIN_DCF77_DATA, INPUT);
```

senza `INPUT_PULLUP`.

Il firmware osserva entrambi i fronti con interrupt `CHANGE` e non applica ancora filtri di protocollo.

## Segnale atteso

Con un modulo funzionante ci aspettiamo:

```text
idle          : LOW
impulso bit 0 : HIGH per circa 100 ms
impulso bit 1 : HIGH per circa 200 ms
periodo       : circa 1000 ms
marker minuto : circa 2000 ms fra due inizi impulso
```

La classificazione 0/1 mostrata nella console è solo diagnostica.

## Console RAW

La pagina web mostra:

- livello DATA HIGH/LOW;
- edge totali;
- edge al secondo;
- numero impulsi;
- larghezza ultimo impulso;
- periodo;
- guess 0/1/?;
- ultimo edge;
- stato P1/PON;
- tabella ultimi impulsi.

Finché gli edge non sono stabili, **il decoder DCF77 non viene considerato attendibile**.

## Perché questa baseline

Un progetto ESP8266 realmente collaudato con ricevitore DCF77 usa PON active-low e ingresso DATA semplice, senza pull-up. Sul suo hardware PON alto produce zero edge, mentre PON basso produce impulsi regolari.

Questa baseline replica prima di tutto quel comportamento elettrico, adattando i pin alla HW-364A.
