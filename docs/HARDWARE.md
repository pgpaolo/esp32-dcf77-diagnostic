# Hardware compatibile

## Target principale

| Componente | Configurazione |
|---|---|
| MCU | ESP8266 NodeMCU / HW-364A |
| Ricevitore | DCF77 77,5 kHz con uscita digitale demodulata |
| Display | SSD1306 128x64 I2C |
| Framework | Arduino ESP8266 |
| Build | PlatformIO |

## Ricevitore DCF77

Il firmware richiede un modulo che fornisca un'uscita digitale con impulsi corrispondenti alla modulazione AM DCF77.

Comportamento atteso:

```text
bit 0 : impulso circa 100 ms
bit 1 : impulso circa 200 ms
sec 59: nessun impulso
```

Non è necessario che il modulo esponga la portante RF a 77,5 kHz.

## Requisiti elettrici

Prima di collegare un modulo verificare sempre:

- tensione VCC ammessa;
- livello logico dell'uscita;
- uscita open-collector/open-drain oppure push-pull;
- necessità di pull-up esterno;
- polarità logica.

Il firmware predefinito usa:

```ini
-D DCF77_ACTIVE_LOW=1
-D DCF77_USE_INTERNAL_PULLUP=1
```

## Configurazioni tipiche

### Uscita active-low open collector

Configurazione consigliata:

```ini
-D DCF77_ACTIVE_LOW=1
-D DCF77_USE_INTERNAL_PULLUP=1
```

### Uscita active-high

```ini
-D DCF77_ACTIVE_LOW=0
```

### Pull-up già presente sul modulo

```ini
-D DCF77_USE_INTERNAL_PULLUP=0
```

## Posizionamento antenna

Per una ferrite DCF77:

- allontanarla dall'ESP8266;
- evitare alimentatori switching vicini;
- evitare convertitori DC/DC;
- evitare cavi USB 3.x vicini;
- provare a ruotare la ferrite di 90°;
- mantenere DATA e GND corti quando possibile.

## Known-good checklist

Una configurazione può essere considerata compatibile quando:

- gli impulsi sono prevalentemente 100/200 ms;
- il periodo normale è circa 1 s;
- il marker produce circa 2 s;
- il jitter resta basso;
- la percentuale impulsi validi resta elevata;
- il frame raggiunge 59 bit;
- P1/P2/P3 risultano corrette;
- ora e data vengono decodificate stabilmente.

## Ricevitori da documentare

Quando viene validato un nuovo modulo aggiungere:

| Modulo | VCC | Polarità | Pull-up | Esito | Note |
|---|---:|---|---|---|---|
| Ricevente DCF77 dedicata 77,5 kHz | da verificare sul modello | da verificare | da verificare | in collaudo | nuova ricevente definitiva |
