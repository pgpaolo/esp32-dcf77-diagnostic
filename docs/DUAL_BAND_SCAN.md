# Scansione diagnostica DCF77 / MSF

DCF77 usa 77,5 kHz; MSF inglese usa 60 kHz. La selezione del decoder nel
portale non modifica da sola la frequenza RF del ricevitore. La modalità
`RAW 60 kHz` del firmware è un analizzatore generico, non un decoder MSF.

Il modulo fotografato è marcato MASO-S-R1, con SEL/OUT/PON/GND/VDD.
Non è disponibile un datasheet verificato di questo esatto modulo: **non
attribuire automaticamente LOW a EU e HIGH a UK, o viceversa**.

## Procedura sul profilo HW364A

1. Mantenere PON LOW, OUT INPUT e polarità HIGH; antenna, alimentazione,
   Wi-Fi e OLED nella stessa condizione durante le due prove.
2. Impostare SEL LOW dal portale, attendere l'assestamento del ricevitore e
   registrare 180 s in modalità normale. Salvare il file prima della prova successiva.
3. Impostare SEL HIGH, attendere nuovamente e registrare altri 180 s.
4. Analizzare ogni file con entrambi i protocolli e le due polarità:

   ```sh
   python tools/scan_time_signals.py sel-low.dcfraw --output sel-low.scan.json
   python tools/scan_time_signals.py sel-high.dcfraw --output sel-high.scan.json
   ```

La registrazione resta disponibile selezionando il decoder software DCF77:
il file contiene il livello fisico OUT a 1 kHz, anche quando SEL seleziona
un'altra banda. Nessun nuovo firmware è necessario per questa acquisizione.

Lo scanner prova 100 fasi distanziate di 10 ms. Confronta ogni secondo con
forme d'onda ideali, ammettendo al massimo 70 campioni discordanti e chiedendo
almeno 30 campioni di vantaggio sulla seconda forma candidata. Verifica i
marcatori, la parità, i campi BCD, la data e il giorno della settimana.
`identified` richiede due frame consecutivi con avanzamento di un minuto.

Questo strumento è diagnostico: non recupera bit dal rumore, non misura la
portante RF, non segue derive importanti e non gestisce i minuti con secondi
intercalari. L'assenza di frame validi non prova che la stazione sia assente.
Un ricevitore debole può richiedere più tempo di assestamento/acquisizione.
`matchedPercent` misura una somiglianza a forme ideali, non la qualità RF.

## Riferimento ufficiale MSF

[NPL, MSF Time and Date Code](https://www.npl.co.uk/products-services/time-frequency/msf-radio-time-signal/msf_time_date_code):
il secondo 00 ha 500 ms senza portante. Gli altri secondi iniziano con 100 ms
senza portante; i successivi intervalli da 100 ms portano i bit A e B.
Il caso A=0/B=1 produce due tratti separati e non va interpretato come un
singolo impulso da 200 ms. Il riconoscimento verifica anche l'identificatore
di minuto, quattro parità dispari e la data del minuto successivo in ora UK.

I test generano MSF con impulsi separati, fase arbitraria e polarità inversa,
DCF77 pulito e rumore casuale; verificano anche il rifiuto di parità errate.

## Indicazioni del produttore: applicabilità da verificare

[Micro Analog Systems, MAS6181B, DA6181B.003](https://www.mas-oy.com/wp-content/uploads/2016/05/da6181B.pdf),
pagina 4, documenta un integrato a due bande: PDN1/PDN2 selezionano frequenza
e spegnimento; HIGH/HIGH spegne, HIGH/LOW sceglie la frequenza maggiore,
LOW/HIGH e LOW/LOW la minore. Consiglia almeno 50 ms di spegnimento prima
di cambiare frequenza per inizializzare l'AGC.

**Non è confermato che il MASO-S-R1 usi MAS6181B né come SEL/PON siano
collegati internamente.** Questa tabella non è una mappatura dei suoi pin.
Una successiva prova con riavvio del ricevitore va separata dalla scansione
che modifica soltanto SEL; i risultati devono indicare le condizioni effettive.
