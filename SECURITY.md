# Security Policy

## Ambito

Questo progetto è un ricevitore DCF77 embedded per uso locale.

La console web integrata è pensata per:

- laboratorio;
- rete domestica;
- rete tecnica isolata;
- accesso diretto all'AP del dispositivo.

## Considerazioni importanti

Il portale HTTP non deve essere considerato un'interfaccia Internet-facing.

In particolare:

- non esporre direttamente la porta HTTP su Internet;
- non configurare port-forwarding verso il dispositivo;
- usare una rete fidata;
- proteggere la rete Wi-Fi;
- evitare di inserire credenziali riutilizzate altrove.

Le impostazioni Wi-Fi vengono gestite dal firmware ESP8266; il progetto non implementa un vault o una piattaforma di gestione segreti.

## Segnalazione vulnerabilità

Per problemi di sicurezza non pubblicare immediatamente dettagli sfruttabili in una issue pubblica.

Aprire una segnalazione privata tramite i meccanismi messi a disposizione da GitHub, se disponibili, oppure contattare il maintainer del repository.

## Firmware

Prima di distribuire una build:

1. verificare che la CI sia verde;
2. verificare il commit esatto;
3. evitare binari provenienti da fonti non controllate;
4. preferire build riproducibili da sorgente con PlatformIO.

## Supporto

Il progetto è mantenuto su base volontaria e non fornisce garanzie di sicurezza per esposizioni su reti ostili.
