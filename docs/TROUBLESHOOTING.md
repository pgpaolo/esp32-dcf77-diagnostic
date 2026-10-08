<!--
DCF77 RC8000 Console v2.5.4
Copyright (c) 2026 Gianpaolo P.
Licensed under the PolyForm Noncommercial License 1.0.0.
Commercial use requires a separate written license from the copyright holder.
See LICENSE and NOTICE.md.
-->

# Troubleshooting

## OLED spento durante SEARCH

È **normale** in v2.5.4. Il display viene tenuto RF-quiet finché la radio è accesa. Deve accendersi quando il duty timer spegne il ricevitore dopo i sync validi.

## OLED `NON RILEVATO`

Il firmware prova:

- GPIO14 SDA / GPIO12 SCL;
- GPIO12 SDA / GPIO14 SCL;
- 0x3C e 0x3D;
- SSD1306 e fallback SH1106.

Se non riceve ACK, controllare alimentazione della board/display e variante hardware.

## CLOCK 1 Hz LOCK ma frame KO

Controllare:

- `invalidSlots`, `lostBitGaps`;
- missing prima/dopo recovery;
- quale blocco di parità contiene più di un missing;
- presenza di marker inferiti e reject fuori fase.

Un lock stabile non garantisce automaticamente che tutti i bit utili siano determinabili.

## Qualità bassa / molti spike

- tenere OLED spento durante RX (default 2.5.4);
- evitare scansioni Wi-Fi;
- eseguire `RF QUIET 60 s` e confrontare;
- provare AUTO A/B INPUT vs INPUT_PULLUP;
- spostare/ruotare ferrite;
- migliorare disaccoppiamento e distanza da USB/switching.

## Ricevitore non produce fronti

Verificare PON: GPIO5 deve andare LOW quando `receiverOn=true` nella configurazione active-low. Verificare 3.3 V e massa comune sul modulo RC8000.

## Frame con 2-3 missing

Può essere salvato solo se gli erasure sono distribuiti in blocchi indipendenti e ogni blocco ha al massimo un unknown. Tre missing nello stesso blocco non sono ricostruibili con una singola parità.

## Wi-Fi non si connette

Attendere il fallback AP `DCF77-Setup-XXXXXX`, collegarsi e aprire `192.168.4.1`.

## Timer non spegne la radio

Serve il numero configurato di **frame completi con ora e data valide consecutivamente**. Un frame KO azzera il contatore consecutivo.
