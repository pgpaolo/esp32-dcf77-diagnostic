# MSF UK a 60 kHz sul profilo HW364A

Il profilo ESP8266 ora avvia il decoder software MSF UK. Il portale conserva
anche i pulsanti DCF77 e RAW 60 kHz; selezionare un decoder non commuta
automaticamente SEL. Il modulo JJY 40/60 kHz dispone di una banda RF a 60 kHz
utilizzabile per tentare MSF, senza sostituire quarzi o antenna.

Non è ancora verificata la corrispondenza SEL LOW/HIGH con 40/60 kHz del
MASO-S-R1: confrontare entrambe le posizioni. Per MAS6181B la combinazione
PDN1 HIGH / PDN2 LOW seleziona la frequenza maggiore, ma l'associazione dei
controlli del modulo ai pin dell'integrato richiede verifica. START PON mantiene
il controllo PON HIGH per 3 s e poi lo riporta LOW; non identifica da solo la banda.

Compilazione e caricamento:

```sh
pio run -e hw364a
pio run -e hw364a -t upload --upload-port COM8
```

La decodifica usa gli stessi campioni hardware di OUT a 1 kHz dello scope,
raggruppati in celle da 10 ms, prima dei filtri DCF77. Prova le fasi del
secondo e confronta cinque forme: A/B=00, 01 (impulsi separati), 10, 11,
e marker da 500 ms. Segue l'identificatore di minuto, controlla quattro
parità dispari, BCD, data e giorno della settimana. Due minuti consecutivi
coerenti sono necessari per confermare l'orologio. La data trasmessa riguarda
il minuto successivo; il riferimento temporale deriva dalla fase acquisita.

Portale e OLED riportano **ora civile UK, GMT/BST**, non l'ora italiana.
Il portale espone bit A/B, fase, frame e orologio. Le registrazioni grezze
normal/quiet sono disponibili anche in MSF. La polarità resta configurabile.

È un primo decoder rigoroso: non recupera bit mancanti dal rumore, non
gestisce minuti da 59/61 secondi e riacquisisce dopo un cambio GMT/BST.
Le tolleranze di confronto non coprono tutte le distorsioni possibili del
ricevitore. Gli impulsi grezzi e i file registrati restano disponibili per
diagnosticare un mancato aggancio. Una compilazione o un test simulato
superato non prova la ricezione radio inglese sul dispositivo.

Fonte primaria: [NPL MSF Time and Date Code](https://www.npl.co.uk/products-services/time-frequency/msf-radio-time-signal/msf_time_date_code).
Test del codice C++ reale: `tools/tests/msf_regression.cpp`, con fase a cavallo
delle finestre, A=0/B=1, parità errata, perdita di finestre e rollover micros.
