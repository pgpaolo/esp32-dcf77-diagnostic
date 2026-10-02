# Ricevitore DCF77: riferimenti e criteri di diagnosi

Ricerca verificata il 2 ottobre 2026. Il protocollo è semplice quando OUT
presenta una sequenza regolare; la ricezione disturbata richiede anche ricerca
di fase, controllo dei frame e conferma temporale.

## Indicazioni del produttore Micro Analog Systems

Fonte: [MAS6180B, DA6180B.005, 5 febbraio 2021](https://www.mas-oy.com/wp-content/uploads/2016/04/DA6180B_005.pdf).
**Non è accertato che il MASO-S-R1 monti questo integrato.** Non trasferire
automaticamente le specifiche del chip ai pin SEL/PON del modulo.

| Dato del MAS6180B | Riferimento |
|---|---|
| Alimentazione operativa 1,1–3,6 V | pagina 3 |
| OUT alto durante la riduzione della portante, basso a portante piena | pagina 2 |
| OUT genera e assorbe corrente; minimo specificato 5 µA | pagine 2–3 |
| PDN basso: acceso; alto: spento; fronte alto→basso avvia fast startup | pagina 2 |
| AON alto o scollegato: AGC attivo; basso: guadagno mantenuto | pagina 2 |
| Impulso zero: 40–130 ms; impulso uno: 140–250 ms | pagina 8, tabella 5 |

Queste finestre sono tolleranze software raccomandate, non le durate nominali
trasmesse. Il produttore indica che rumore e livello ricevuto modificano le durate.
OUT non è descritto come semplice open collector: un pull-up può caricare
un'uscita debole. Sul profilo HW364A resta quindi `INPUT`, senza pull-up.
`INPUT_PULLUP` è una possibilità diagnostica subordinata al tipo di uscita reale.

## Applicazione nel firmware HW364A

- Le finestre in `include/config.h` seguono 40–130 / 140–250 ms. Il percorso
  a fronti conserva il vuoto fra 130 e 140 ms; il campionatore ha risoluzione 10 ms.
- L'acquisizione della fase ammette impulsi da 40 ms anziché richiedere 70 ms.
- Dopo l'aggancio, un impulso continuo che inizia entro ±30 ms dalla fase e
  non contiene altri blocchi nei successivi 300 ms è classificato per durata.
  In particolare 140 ms è uno e 40 ms è zero.
- Per segnali frammentati resta la decisione integrativa sulle due metà da
  100 ms. Le finestre del produttore non costituiscono quindi un limite rigido
  per ogni forma d'onda disturbata. Parità, timezone e conferma di minuti
  consecutivi restano necessarie per la sincronizzazione.
- `pulseMs`/`lastPulseMs` nel percorso campionato rappresentano ancora il
  simbolo nominale 100/200 ms; i blocchi grezzi/filtrati sono metriche separate.

I test eseguono il codice C++ di produzione con stream da 40/140 e 130/250 ms,
anche a cavallo delle finestre. Sono prove simulate: non attestano la ricezione RF.
I profili ESP32 mantengono le precedenti finestre e impostazioni elettriche.

## Soluzioni di altri progettisti

- [Thijs Elenbaas, Arduino-DCF77](https://github.com/thijse/Arduino-DCF77):
  gli esempi `DCFSignal` e `DCFPulseLength` misurano periodi e durate effettive;
  l'autore suggerisce di adattare la soglia fra le due classi ai dati osservati.
- [Udo Klein, dcf77](https://github.com/udoklein/dcf77): decoder resistente
  al rumore con rilevamento di fase e accumulo statistico. Il file
  [dcf77.h](https://github.com/udoklein/dcf77/blob/master/dcf77.h) contiene supporto
  ESP8266 esplicitamente sperimentale. È un candidato per un confronto futuro
  sugli stessi campioni, non una libreria già integrata in questo progetto.
  [Super Filter](https://blog.blinkenlight.net/experiments/dcf77/super-filter/)
  decodifica e rigenera il segnale: richiede comunque una ricezione decodificabile.
- [AZ-Delivery, orologio ESP in MicroPython, parte 6](https://www.az-delivery.de/en/blogs/azdelivery-blog-fur-arduino-und-raspberry-pi/funkwecker-mit-esp-in-micropython-teil-6-dcf77-funkmodul):
  documenta interferenze del proprio display multiplexato e accoppiamento
  fra antenna e filo di uscita. Non dimostra che l'OLED HW364A sia la causa.
- [PTB: codice DCF77](https://www.ptb.de/cms/ptb/fachabteilungen/abt4/fb-44/ag-442/verbreitung-der-gesetzlichen-zeit/dcf77/zeitcode.html):
  riferimento del servizio per durata nominale dei simboli e struttura del codice.

## Prove sul dispositivo

Tenere inizialmente OUT `INPUT`, polarità HIGH e i livelli SEL/PON già usati.
Registrare diversi minuti di OUT, stato della fase e frame validi.
Modificare una sola variabile per prova: distanza/orientamento dell'antenna,
alimentazione, poi attività del display e Wi-Fi. Chiudere il portale non spegne
il Wi-Fi; oscurare lo schermo non garantisce l'arresto del bus/display.
Confrontare sequenze di impulsi e frame, non la sola percentuale di qualità.
Un cablaggio corretto non esclude disturbi, ma nessuna di queste cause è
dimostrata finché non viene misurata una differenza ripetibile.
