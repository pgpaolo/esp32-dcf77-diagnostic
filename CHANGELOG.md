# Changelog

Tutte le modifiche rilevanti del progetto sono documentate in questo file.

Il formato segue lo stile di [Keep a Changelog](https://keepachangelog.com/) e il progetto usa versioni semantiche quando viene pubblicata una release.

## [Unreleased]

### Added
- decoder DCF77 dedicato per ESP8266 HW-364A;
- modalità **DIRETTA / strict**;
- modalità **ACCUMULO / radio clock** con confidenza per bit e verifica di continuità fra minuti;
- decodifica BCD di minuti, ore, data, mese, anno e giorno della settimana;
- controllo P1/P2/P3;
- gestione CET/CEST;
- console web con griglia grafica dei 59 bit;
- diagnostica avanzata: jitter RMS, rapporto impulsi validi, glitch, heap, RSSI, età ultimo frame valido;
- selezione persistente della vista OLED;
- API HTTP locale;
- controllo hardware PON del ricevitore su D1/GPIO5 con ON, OFF e restart 3 s dal portale web;
- documentazione tecnica completa;
- CI GitHub Actions con PlatformIO.

### Changed
- progetto semplificato per una vera ricevente DCF77 77,5 kHz con uscita digitale;
- `hw364a` impostato come environment PlatformIO predefinito;
- eliminati i vecchi percorsi sperimentali legati a ricevitori non corretti, MSF/60 kHz, PLL RAW e ricostruzioni software del segnale.

### Fixed
- gestione include Wi-Fi nella schermata diagnostica OLED;
- sincronizzazione del decoder dopo marker minuto;
- persistenza modalità decoder e visualizzazione OLED.

## Prima release stabile

La prima release verrà creata dopo il collaudo prolungato con la ricevente DCF77 definitiva. Il firmware binario non viene ancora distribuito.
