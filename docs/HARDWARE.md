# Hardware — v2.5.4

## Componenti

La configurazione di riferimento usa **due elementi distinti**:

1. **HW-364A**: board ESP8266 con OLED integrato 0.96" 128x64.
2. **RC8000 / DCF-3850N-800**: ricevitore DCF77 esterno a 77.5 kHz con uscita digitale DATA e comando PON.

Questa distinzione è importante: HW-364A non è il ricevitore radio DCF77.

## Pinout usato dal firmware

| Segnale | GPIO ESP8266 | Direzione | Stato / comportamento |
|---|---:|---|---|
| `DCF_DATA_PIN` | 13 | IN | impulsi del ricevitore; default `INPUT` |
| `DCF_PON_PIN` | 5 | OUT | active LOW: LOW=ON, HIGH=OFF |
| `OLED_SDA_PIN` | 14 | I2C | SDA OLED integrato |
| `OLED_SCL_PIN` | 12 | I2C | SCL OLED integrato |
| OLED address | — | I2C | 0x3C default; probe 0x3D |

Il firmware prova anche SDA/SCL invertiti (GPIO12/GPIO14) perché sono stati osservati batch HW-364A con documentazione/cablaggio non uniforme.

## OLED

Target primario: SSD1306 128x64. È presente un fallback SH1106. Durante l'acquisizione DCF77 il display viene spento; con SSD1306 viene disabilitata anche la charge pump per ridurre ulteriormente il rumore del pannello.

## DCF77 receiver

Il firmware assume:

- carrier ricevuta dal modulo esterno a 77.5 kHz;
- DATA digitale con impulsi DCF;
- PON active-low;
- alimentazione a 3.3 V nel banco di riferimento;
- massa comune con ESP8266.

## Alimentazione e disaccoppiamento

DCF77 lavora con segnali molto deboli. Sul ricevitore è consigliato un disaccoppiamento locale vicino ai pin di alimentazione (es. 100 nF ceramico + capacità elettrolitica/bulk adeguata al montaggio). Evitare loop di massa lunghi e alimentatori switching rumorosi a ridosso della ferrite.

## Criticità elettromagnetiche osservate

Nel sistema testato sono risultati rilevanti:

- OLED/charge pump e traffico I2C;
- attività Wi-Fi e soprattutto scansioni;
- ESP8266 e cablaggi digitali molto vicini alla ferrite;
- alimentazione USB / switching;
- orientamento dell'antenna in ferrite.

Per questo la 2.5.4 usa una separazione temporale netta: **radio ON = OLED OFF**, **radio OFF = OLED ON**.

## Riferimenti HW-364A

Esempi pubblici riportano OLED 128x64, indirizzo 0x3C e linee su GPIO14/GPIO12. Poiché le etichette D5/D6 sono riportate in modo non uniforme da venditori e progetti, questa documentazione usa sempre i numeri GPIO.

- https://github.com/Bl4d3hUnt3r/HW364-A
- https://github.com/dzwiedziu-nkg/nodemcu-with-oled-example
