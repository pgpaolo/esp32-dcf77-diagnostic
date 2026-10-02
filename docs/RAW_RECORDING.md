# Registrare OUT e confrontare i decoder

Disponibile nel profilo `hw364a`. Prima caricare il firmware con
`pio run -e hw364a -t upload`, poi riaprire il portale.

## Procedura sul dispositivo

1. Conservare cablaggio, alimentazione, antenna e livelli SEL/PON/OUT/polarità.
2. Nel pannello **Registrazione OUT · 3 minuti**, avviare la prova con
   Wi-Fi/OLED attivi. Attendere la fine e scaricare il file, rinominandolo
   `out-normal.dcfraw`.
3. Avviare la prova con **Wi-Fi/OLED spenti**, senza spostare l'antenna.
   Il portale diventa irraggiungibile intenzionalmente. Attendere circa 185 s;
   OLED, AP e connessione STA vengono riattivati automaticamente. Se il router
   assegna un IP diverso, usare l'AP `DCF77-HW364A-xxxxxx`, su `192.168.4.1`.
4. Scaricare la seconda prova come `out-quiet.dcfraw`. Ripetere il confronto
   per verificare che un'eventuale differenza sia riproducibile.

La prova silenziosa confronta insieme Wi-Fi e OLED: da sola non identifica
quale dei due sia responsabile di una differenza. Una successiva prova dovrà
isolare le singole variabili. Il display riceve DISPLAYOFF e il firmware
sospende gli aggiornamenti I²C; il Wi-Fi passa realmente a WIFI_OFF.

Durante la registrazione i comandi che cambiano ricevitore, decoder, polarità
e rete sono bloccati. La registrazione normale può essere interrotta dal portale;
in quella silenziosa inviare `x` sulla seriale, oppure attendere il ripristino.
Il timeout di sicurezza termina dopo 190 s dall'inizio del campionamento.
Un riavvio ripristina il servizio ma **perde la registrazione**.

## Dati e limiti

Il timer salva 180.000 livelli fisici HIGH/LOW a 1 kHz prima del filtro, in
22.500 byte di RAM. Non scrive flash durante la prova. Ogni nuova registrazione
sostituisce la precedente; i dati restano in RAM fino alla prova successiva o
al riavvio. Se manca un blocco libero sufficiente, l'avvio è rifiutato prima
di spegnere la rete. Riavviare e riprovare dopo aver scaricato i dati esistenti.

Formato `DCFRAW1`: una riga JSON seguita dai bit, LSB-first in ciascun byte;
1 = livello fisico HIGH. Il JSON include numero dei campioni, polarità,
SEL/PON, pull-up, modalità normal/quiet, durata reale fra primo e ultimo
campione, numero di intervalli >1.500 µs e intervallo massimo.
La presenza di buchi temporali non viene nascosta: il replay uniforme li
rifiuta. Le singole posizioni dei buchi non sono registrate.

API: GET `/api/recording`, POST `/api/recording/start` con `mode=normal|quiet`,
POST `/api/recording/stop`, GET `/api/recording/download`.

## Confronto offline

Su Linux con Python 3, Git e g++:

```sh
git clone https://github.com/udoklein/dcf77.git /tmp/udo-dcf77
git -C /tmp/udo-dcf77 checkout 00d2450f47311268ba5de17f35480781928b34dc
python tools/compare_recording.py out-normal.dcfraw --udo-dir /tmp/udo-dcf77
python tools/compare_recording.py out-quiet.dcfraw --udo-dir /tmp/udo-dcf77
```

Lo strumento esegue il campionatore/decoder C++ del progetto e il decoder
originale di Udo Klein su **tutti gli stessi campioni**, normalizzati secondo
la polarità salvata. Non converte lo scope da 10 ms in un segnale inventato.
Il riferimento Udo è un eseguibile host separato GPL-3.0-or-later; la libreria
non viene incorporata nel firmware. La revisione è fissata e verificata.
Il timer hardware e la correzione dell'oscillatore non sono simulati dal replay.

Il report restituisce fase/minuto/frame/clock del progetto e stato finale/
tempo trascorso in `synced` per Udo (stati 0..5, con 5 = synced). Le metriche
non hanno scale equivalenti. Tre minuti senza lock di entrambi non provano
un guasto hardware: con rumore Udo può richiedere acquisizioni più lunghe.

La CI verifica formato, file troncati, buchi temporali, registrazione e stop,
e confronta entrambi i decoder su otto minuti sintetici puliti. I test non
dimostrano il risultato RF della prova silenziosa sulla scheda reale.

Riferimento: [descrizione dell'autore](https://blog.blinkenlight.net/experiments/dcf77/dcf77-library/).
Il suo decoder riceve un livello logico ogni millisecondo; lo scope serve a
verificare l'ingresso prima di valutare la decodifica.

## Indicatori del portale

`quality` è `null` quando mancano simboli recenti o la fase DCF77 non è
agganciata; `historicalQuality` conserva la precedente media per analisi.
Anche OLED evita di mostrare una confidenza vecchia durante SEARCH.
I contatori validi/invalidi ora contano simboli classificati prima del lock
del minuto. I marker provvisori non sono conteggiati come impulsi invalidi.
I fronti grezzi su OUT e i frame con parità valida restano misure distinte.
