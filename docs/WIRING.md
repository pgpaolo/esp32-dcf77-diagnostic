<!--
DCF77 RC8000 Console v2.5.4
Copyright (c) 2026 Gianpaolo P.
Licensed under the PolyForm Noncommercial License 1.0.0.
Commercial use requires a separate written license from the copyright holder.
See LICENSE and NOTICE.md.
-->

# Cablaggio

## Collegamenti RC8000 ↔ HW-364A

```text
RC8000 / DCF-3850N-800          HW-364A / ESP8266
--------------------------------------------------
VDD / 3.3 V              --->   3V3
GND                      --->   GND
DATA                     --->   GPIO13
PON                      --->   GPIO5
```

`PON` è configurato **active LOW**:

- GPIO5 = LOW → ricevitore acceso;
- GPIO5 = HIGH → ricevitore spento.

L'OLED è integrato sulla HW-364A e non richiede cablaggio esterno. Il firmware usa GPIO14/GPIO12 come mappa primaria e prova anche l'ordine inverso.

## Note pratiche

- Verificare 3.3 V tra VDD e GND direttamente sul ricevitore.
- Usare massa comune.
- Tenere DATA/PON corti e lontani dalla ferrite quando possibile.
- Evitare di far passare cavi USB/alimentazione switching sopra l'antenna.
- Se DATA produce troppi fronti spuri, usare il test AUTO A/B prima di cambiare hardware.
