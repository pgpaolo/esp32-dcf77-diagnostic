#include "web_portal.h"
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "sampled_dcf77.h"
#include "raw_recording.h"

namespace {
ESP8266WebServer server(80);
const DCF77Decoder *currentDecoder = nullptr;
uint32_t observedPulses = 0, lastPulseMs = 0;
uint32_t rateSampleMs = 0, rateSamplePulses = 0;
float pulseEventsPerSecond = 0.0f;
bool receiverResetRequested = false;
uint32_t receiverChangedMs = 0;
bool outPullupEnabled = DCF77_USE_INTERNAL_PULLUP;
bool dcfActiveLowSelected = DCF77_ACTIVE_LOW;
bool ponStartActive = false;
uint32_t ponStartBeganMs = 0;
constexpr uint32_t PON_START_HIGH_MS = 3000;
enum class PinDriveMode : uint8_t { FLOATING, LOW_LEVEL, HIGH_LEVEL };
PinDriveMode selMode = PinDriveMode::FLOATING;
PinDriveMode ponMode = PinDriveMode::LOW_LEVEL;
SignalMode selectedSignalMode = SignalMode::MSF_60KHZ;
bool recordBusy = false, recordQuiet = false, radioPaused = false, recordCancelled = false;
uint8_t recordStage = 0;
uint32_t recordStageMs = 0, recordStartedMs = 0;
bool recordActiveLow = false, recordPullup = false;
PinDriveMode recordSel = PinDriveMode::FLOATING, recordPon = PinDriveMode::LOW_LEVEL;
const char *modeLabel(PinDriveMode mode);

bool rejectDuringRecording() {
    if (!recordBusy) return false;
    server.send(409,"application/json","{\"ok\":false,\"message\":\"Registrazione in corso: attendere o interrompere\"}");
    return true;
}

String recordingInfo() {
    String json; json.reserve(500);
    json = "{\"format\":\"DCFRAW1\",\"sampleRateHz\":1000,\"encoding\":\"physical-high-lsb-first\",\"samples\":";
    json += rawRecordCount();
    json += ",\"durationUs\":"; json += rawRecordDurationUs();
    json += ",\"timingGaps\":"; json += rawRecordTimingGaps();
    json += ",\"maxGapUs\":"; json += rawRecordMaxGapUs();
    json += ",\"activeLow\":"; json += recordActiveLow ? "true" : "false";
    json += ",\"pullup\":"; json += recordPullup ? "true" : "false";
    json += ",\"quiet\":"; json += recordQuiet ? "true" : "false";
    json += ",\"busy\":"; json += recordBusy ? "true" : "false";
    json += ",\"cancelled\":"; json += recordCancelled ? "true" : "false";
    json += ",\"complete\":"; json += !recordBusy && rawRecordCount()==RAW_RECORD_SAMPLES ? "true" : "false";
    json += ",\"sel\":\""; json += modeLabel(recordSel);
    json += "\",\"pon\":\""; json += modeLabel(recordPon);
    json += "\",\"firmware\":\"recording-v1\"}";
    return json;
}

void recordingStatus() {
    server.sendHeader("Cache-Control","no-store");
    server.send(200,"application/json",recordingInfo());
}
void startRecording() {
    if (rejectDuringRecording()) return;
    const String mode = server.arg("mode");
    if ((mode != "normal" && mode != "quiet") || ponStartActive ||
        selectedSignalMode == SignalMode::RAW_60KHZ) {
        server.send(400,"application/json","{\"ok\":false,\"message\":\"Scegli normal/quiet in DCF77 o MSF e attendi la fine di START PON\"}"); return;
    }
    // Fail before shutting down the network; leave a reserve for HTTP/decoder.
    if ((!rawRecordData() && (ESP.getFreeHeap() < RAW_RECORD_BYTES + 9000 ||
         ESP.getMaxFreeBlockSize() < RAW_RECORD_BYTES)) || !rawRecordPrepare()) {
        server.send(503,"application/json","{\"ok\":false,\"message\":\"RAM insufficiente: riavviare il dispositivo e riprovare\"}"); return;
    }
    recordQuiet = mode == "quiet"; recordCancelled = false;
    recordActiveLow = dcfActiveLowSelected; recordPullup = outPullupEnabled;
    recordSel = selMode; recordPon = ponMode;
    recordBusy = true; recordStage = 1; recordStageMs = millis();
    server.send(202,"application/json","{\"ok\":true,\"message\":\"Registrazione di 180 s avviata. In quiet il portale torna dopo circa 185 s; recupero su 192.168.4.1. La nuova registrazione sostituisce la precedente.\"}");
}
void stopRecording() {
    recordCancelled = true;
    rawRecordStop();
    if (recordBusy) recordStage = 4;
    server.send(200,"application/json","{\"ok\":true}");
}
void downloadRecording() {
    if (rejectDuringRecording()) return;
    const uint32_t count = rawRecordCount();
    if (!count) { server.send(404,"text/plain","Nessuna registrazione"); return; }
    const String header = recordingInfo() + "\n";
    const size_t bytes = (count + 7) / 8;
    server.sendHeader("Content-Disposition","attachment; filename=dcf77-out.dcfraw");
    server.sendHeader("Cache-Control","no-store");
    server.setContentLength(header.length() + bytes);
    server.send(200,"application/octet-stream","");
    server.sendContent(header);
    for (size_t offset=0; offset<bytes && server.client().connected(); offset+=512) {
        const size_t length = bytes-offset < 512 ? bytes-offset : 512;
        server.sendContent(reinterpret_cast<const char *>(rawRecordData()+offset),length);
        yield();
    }
}

// Sampled scope (MIT-compatible independent implementation inspired by the
// troubleshooting method used by Udo Klein and other DCF77 projects).
// 1 ms sampling, 100 bins x 10 ms over one second.
uint8_t scopeBins[100] = {};
uint8_t scopeLastBins[100] = {};
uint16_t scopeTick = 0;
uint32_t scopeNextSampleUs = 0;
uint32_t scopeWindowStartUs = 0;
bool scopeReady = false;
uint16_t scopeLastSamples = 0;
uint16_t scopeLastActiveSamples = 0;

void resetScope();
void pollScope();

const char *modeLabel(PinDriveMode mode) {
    if (mode == PinDriveMode::FLOATING) return "FLOAT";
    return mode == PinDriveMode::HIGH_LEVEL ? "HIGH" : "LOW";
}

void applyPinMode(uint8_t pin, PinDriveMode mode, const char *name) {
    if (mode == PinDriveMode::FLOATING) {
        pinMode(pin, INPUT);
        Serial.printf("MASO %s: GPIO%d -> FLOAT / input\n", name, pin);
        return;
    }
    digitalWrite(pin, mode == PinDriveMode::HIGH_LEVEL ? HIGH : LOW);
    pinMode(pin, OUTPUT);
    Serial.printf("MASO %s: GPIO%d -> %s\n", name, pin, modeLabel(mode));
}

void applySelMode(PinDriveMode mode) {
    selMode = mode;
    applyPinMode(PIN_RX_BAND, mode, "SEL");
}

void applyPonMode(PinDriveMode mode) {
    ponMode = mode;
    applyPinMode(PIN_RX_PON, mode, "PON");
}

void resetReceiverDiagnostics(bool cancelPonStart = true) {
    if (cancelPonStart) ponStartActive = false;
    receiverChangedMs = millis();
    receiverResetRequested = true;
    observedPulses = 0;
    lastPulseMs = 0;
    rateSampleMs = 0;
    rateSamplePulses = 0;
    pulseEventsPerSecond = 0.0f;
    resetScope();
}

void applyOutInputMode(bool pullup) {
    outPullupEnabled = pullup;
    pinMode(PIN_DCF77, pullup ? INPUT_PULLUP : INPUT);
    Serial.printf("MASO OUT: GPIO%d -> %s\n", PIN_DCF77, pullup ? "INPUT_PULLUP" : "INPUT");
    resetReceiverDiagnostics();
}

void resetScope() {
    sampledDcfReset();
}
void pollScope() {
    sampledDcfPoll();
}

char scopeChar(uint8_t v) {
    if (v == 0) return '-';
    if (v >= 10) return 'X';
    return static_cast<char>('0' + v);
}

String jsonEscape(const String &value) {
    String out;
    out.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<uint8_t>(c) >= 0x20) out += c;
                break;
        }
    }
    return out;
}

const char page[] PROGMEM = R"HTML(<!doctype html><html lang="it"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DCF77 / MSF HW-364A</title>
<style>
body{font:16px system-ui;background:#10202d;color:#eaf3fa;max-width:900px;margin:auto;padding:20px}
h1{font-size:24px}h2{margin-top:28px}#clock{font-size:40px}
.card,dl{background:#193346;padding:20px;border-radius:12px;margin:14px 0}
dl{display:grid;grid-template-columns:1fr 1fr;gap:12px}dd{margin:0;text-align:right}
#status{color:#ffd580}.muted{color:#9fc1d4;font-size:14px}
.row{display:flex;gap:10px;flex-wrap:wrap}.row>*{flex:1 1 180px}
select,input,button{box-sizing:border-box;width:100%;padding:11px;border-radius:8px;border:1px solid #3c6077;background:#0d1a23;color:#eef7fc}
button{cursor:pointer;background:#245a78}button:disabled{opacity:.55;cursor:default}
.ok{color:#8fe3a1}.warn{color:#ffd580}.err{color:#ff9a9a}
pre{white-space:pre-wrap;overflow-wrap:anywhere}
table{width:100%;border-collapse:collapse;font-size:14px;background:#193346;border-radius:12px;overflow:hidden}
th,td{padding:8px 10px;border-bottom:1px solid #2b4b5e;text-align:right}
th:first-child,td:first-child{text-align:left}
.good{color:#8fe3a1}.bad{color:#ff9a9a}.neutral{color:#ffd580}
</style>
<h1>DCF77 / MSF · HW-364A</h1>
<div id="clock">--:--:--</div><p id="date">Attesa ricezione</p><p id="status">Connessione…</p>

<div class="card">
<h2 style="margin-top:0">Rete locale</h2>
<p class="muted">L'access point di configurazione resta disponibile su <b>192.168.4.1</b>. Puoi collegare il dispositivo anche alla rete Wi-Fi locale per consultare il troubleshooting dall'IP LAN.</p>
<div id="wifiState" class="warn">Verifica stato Wi-Fi…</div>
<div class="row" style="margin-top:12px">
<div><label for="ssid">Rete Wi-Fi</label><select id="ssid"><option value="">Premi "Scansiona reti"</option></select></div>
<div><label for="pass">Password rete</label><input id="pass" type="password" autocomplete="current-password" placeholder="Lascia vuoto per rete aperta"></div>
</div>
<div class="row" style="margin-top:12px">
<div><button id="scanBtn" type="button" onclick="scanNetworks()">Scansiona reti</button></div>
<div><button id="connectBtn" type="button" onclick="connectWifi()">Connetti alla rete selezionata</button></div>
</div>
<p id="wifiMsg" class="muted"></p>
</div>

<div class="card">
<h2 style="margin-top:0">Ricevitore MASO-S-R1</h2>
<p class="muted">La serigrafia PCB riporta <b>SEL · OUT · PON · GND · VDD</b>. Poiché SEL/OUT risultano fisicamente ambigui sul connettore, il portale li tratta come segnali da verificare e non associa automaticamente SEL a una frequenza.</p>
<div id="receiverState" class="warn">Verifica stato ricevitore…</div>

<p><b>SEL · D2 / GPIO4</b></p>
<div class="row">
<div><button type="button" onclick="setReceiverPin('sel','float')">SEL FLOAT</button></div>
<div><button type="button" onclick="setReceiverPin('sel','low')">SEL LOW</button></div>
<div><button type="button" onclick="setReceiverPin('sel','high')">SEL HIGH</button></div>
</div>

<p><b>Decoder / analizzatore</b></p>
<div class="row">
<div><button type="button" onclick="setDecoder('dcf77')">DCF77 · 77,5 kHz</button></div>
<div><button type="button" onclick="setDecoder('msf60')">MSF UK · 60 kHz</button></div>
<div><button type="button" onclick="setDecoder('raw60')">RAW · 60 kHz</button></div>
</div>
<p class="muted">SEL e decoder sono indipendenti: questo evita di assumere LOW=77,5 kHz o HIGH=60 kHz finché il MASO-S-R1 non è identificato con certezza.</p>

<p><b>PON / ENABLE · D1 / GPIO5</b></p>
<div class="row">
<div><button type="button" onclick="setReceiverPin('pon','float')">PON FLOAT</button></div>
<div><button type="button" onclick="setReceiverPin('pon','low')">PON LOW</button></div>
<div><button type="button" onclick="setReceiverPin('pon','high')">PON HIGH</button></div>
</div>
<p><b>OUT · D7 / GPIO13</b></p>
<div class="row">
<div><button type="button" onclick="setOutMode('input')">OUT INPUT</button></div>
<div><button type="button" onclick="setOutMode('pullup')">OUT INPUT_PULLUP</button></div>
</div>
<p><b>Polarità OUT</b></p>
<div class="row">
<div><button type="button" onclick="setPolarity('low')">ACTIVE LOW</button></div>
<div><button type="button" onclick="setPolarity('high')">ACTIVE HIGH</button></div>
</div>

<p><b>Sequenza di avvio PON</b></p>
<div class="row">
<div><button id="ponStartBtn" type="button" onclick="startPon()">START PON · HIGH 3 s → LOW</button></div>
</div>
<p id="receiverMsg" class="muted"></p>
</div>

<dl id="metrics"></dl>

<h2>Registrazione OUT · 3 minuti</h2>
<p class="muted">Conserva 180.000 campioni grezzi in RAM. Una nuova prova sostituisce la precedente; scaricala prima. Nella prova silenziosa Wi-Fi e OLED si spengono e tornano automaticamente dopo circa 185 secondi. Se l'IP LAN cambia, collegati all'AP su 192.168.4.1. Il riavvio perde i dati.</p>
<div class="row">
<button type="button" onclick="startRecord('normal')">Registra con Wi-Fi/OLED attivi</button>
<button type="button" onclick="startRecord('quiet')">Registra con Wi-Fi/OLED spenti</button>
<button type="button" onclick="stopRecord()">Interrompi registrazione</button>
</div>
<p id="recordState" class="muted">Nessuna registrazione</p>
<a href="/api/recording/download" download>Scarica registrazione OUT</a>

<h2>Scope OUT · 1 secondo</h2>
<p class="muted">Campionamento hardware di OUT a 1 kHz, indipendente da Wi-Fi/web/OLED. 100 celle da 10 ms: "-" = inattivo, 1..9 = attività parziale, X = attivo per tutta la cella. La stessa acquisizione alimenta il decoder DCF77 o MSF selezionato.</p>
<pre id="scopeLine">Attesa primo secondo completo…</pre>
<p id="scopeInfo" class="muted"></p>

<h2>Monitor simboli / impulsi</h2>
<p class="muted">In modalità DCF77 mostra i simboli prodotti dal decoder campionato (secondo, bit e confidenza). In MSF mostra il bit A e il marker MIN; i bit A/B sono nelle metriche. In RAW 60 kHz mostra gli impulsi grezzi.</p>
<table>
<thead><tr><th>Età</th><th>Sorgente</th><th>Secondo</th><th>Impulso ms</th><th>Periodo ms</th><th>Bit</th><th>Conf.</th><th>Timing</th><th>Frame</th></tr></thead>
<tbody id="pulseRows"><tr><td colspan="6">Attesa impulsi…</td></tr></tbody>
</table>

<h2>Frame corrente</h2><pre id="liveFrame">—</pre>
<h2>Ultimo frame completato</h2><pre id="frame">—</pre>

<script>
let quietUntil=0;
async function startRecord(mode){
  const el=document.getElementById('recordState');
  try{
    const r=await fetch('/api/recording/start',{method:'POST',body:new URLSearchParams({mode})});
    const d=await r.json();el.textContent=d.message;
    if(r.ok&&mode==='quiet')quietUntil=Date.now()+185000;
  }catch(e){el.textContent='Avvio non confermato: controllare lo stato della registrazione'}
}
async function stopRecord(){
  if(Date.now()<quietUntil){document.getElementById('recordState').textContent='Wi-Fi spento: attendi il ripristino automatico oppure invia x tramite seriale.';return}
  await fetch('/api/recording/stop',{method:'POST'});updateRecord();
}
async function updateRecord(){
  if(document.hidden)return;
  const el=document.getElementById('recordState');
  if(Date.now()<quietUntil){el.textContent='Prova silenziosa: portale sospeso, ritorno previsto fra '+Math.ceil((quietUntil-Date.now())/1000)+' s';return}
  try{
    const r=await fetch('/api/recording',{cache:'no-store'});const d=await r.json();
    el.textContent=(d.busy?'Registrazione in corso':(d.complete?'Registrazione completa':(d.samples?'Registrazione parziale':'Nessuna registrazione')))+' · '+(d.samples/1000).toFixed(1)+' s di campioni · anomalie temporali '+d.timingGaps+(d.samples?' · '+(d.quiet?'Wi-Fi/OLED spenti':'Wi-Fi/OLED attivi'):'');
  }catch(e){el.textContent='Dispositivo non raggiungibile: attendere il ripristino o usare 192.168.4.1'}
}
setInterval(updateRecord,2000);updateRecord();
const labels={msfBitA:'MSF ultimo bit A',msfBitB:'MSF ultimo bit B',acquisitionState:'Stato acquisizione',acquisitionConfidence:'Confidenza acquisizione (%)',fieldConfidence:'Confidenza campi BCD (%)',predictionMatch:'Coerenza predittiva (%)',sampledSymbols:'Simboli campionati',candidateMinutes:'Minuti coerenti',recoveredBits:'Bit recuperati',uncertainBits:'Bit incerti',quality:'Qualità temporale (%)',minuteSynced:'Sincronizzazione minuto',minuteMarkers:'Marker minuto rilevati',frameBitCount:'Posizione frame',lastBit:'Ultimo bit',pulseMs:'Impulso (ms)',periodMs:'Periodo (ms)',jitterMs:'Jitter (ms)',rmsMs:'Jitter RMS (ms)',validPulses:'Impulsi validi',invalidPulses:'Impulsi invalidi',validFrames:'Frame validi',invalidFrames:'Frame invalidi',parityErrors:'Errori parità',timingErrors:'Errori temporali',glitches:'Glitch',frameAgeSeconds:'Età ultimo frame (s)',ppsUs:'Offset PPS (µs)',freeHeap:'RAM libera (byte)'};

async function update(){
  if(document.hidden||Date.now()<quietUntil)return;
  try{
    const r=await fetch('/api/status',{cache:'no-store'});
    if(!r.ok)throw Error();
    const d=await r.json();
    document.getElementById('clock').textContent=d.time||'--:--:--';
    document.getElementById('date').textContent=d.date||'Attesa frame valido';
    document.getElementById('status').textContent=d.signalRecent
      ? (d.phaseLocked ? (d.clockAvailable?'Fase '+d.protocol+' agganciata · orologio disponibile':'Fase secondo agganciata · acquisizione minuto')
          : 'Attività su OUT · segnale '+d.protocol+' non ancora agganciato')
      : (d.clockAvailable?'Nessuna transizione recente · orologio dall’ultima sincronizzazione':'Nessuna transizione recente su OUT');
    const list=document.getElementById('metrics');list.replaceChildren();
    for(const [k,l]of Object.entries(labels)){const a=document.createElement('dt'),b=document.createElement('dd');a.textContent=l;b.textContent=k==='minuteSynced'?(d[k]?'AGGANCIATO':'IN ATTESA'):(d[k]??'—');list.append(a,b)}
    document.getElementById('liveFrame').textContent=d.liveFrame||'Nessun frame corrente';
    document.getElementById('frame').textContent=d.frame||'Nessun frame completato';
  }catch(e){document.getElementById('status').textContent='Connessione al dispositivo persa'}
}

async function updateWifi(){
  if(document.hidden||Date.now()<quietUntil)return;
  try{
    const r=await fetch('/api/wifi',{cache:'no-store'});
    if(!r.ok)throw Error();
    const d=await r.json();
    const el=document.getElementById('wifiState');
    if(d.connected){
      el.className='ok';
      el.textContent='Connesso a '+d.ssid+' · IP LAN '+d.ip+' · RSSI '+d.rssi+' dBm';
    }else{
      el.className='warn';
      el.textContent=d.connecting?'Connessione alla rete locale in corso…':'Non connesso alla rete locale';
    }
  }catch(e){
    const el=document.getElementById('wifiState');el.className='err';el.textContent='Impossibile leggere lo stato Wi-Fi';
  }
}

async function updateReceiver(){
  if(document.hidden||Date.now()<quietUntil)return;
  try{
    const r=await fetch('/api/receiver',{cache:'no-store'});
    if(!r.ok)throw Error();
    const d=await r.json();
    const el=document.getElementById('receiverState');
    el.className=(d.sel==='FLOAT'||d.pon==='FLOAT')?'warn':'ok';
    el.textContent='SEL '+d.sel+' · GPIO '+d.selGpio+
      ' | DECODER '+d.decoderMode+
      ' | PON '+d.pon+' · GPIO '+d.ponGpio+
      ' | OUT '+d.outLevel+' · GPIO '+d.outGpio+' ('+d.outMode+', ACTIVE '+d.polarity+')'+
      ' | fronti grezzi '+d.eventsPerSecond+'/s'+
      ' | da modifica '+d.secondsSinceChange+' s'+
      (d.ponStartActive?' | START PON in corso '+d.ponStartRemainingMs+' ms':'');
  }catch(e){
    const el=document.getElementById('receiverState');el.className='err';el.textContent='Impossibile leggere lo stato del ricevitore';
  }
}

async function setDecoder(mode){
  const msg=document.getElementById('receiverMsg');
  msg.textContent='Impostazione decoder…';
  try{
    const body=new URLSearchParams({mode});
    const r=await fetch('/api/decoder',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
    const d=await r.json();
    msg.textContent=d.message||'Decoder aggiornato';
    updateReceiver();
  }catch(e){msg.textContent='Errore durante la modifica del decoder'}
}

async function setReceiverPin(pin,mode){
  const msg=document.getElementById('receiverMsg');
  msg.textContent='Impostazione '+pin.toUpperCase()+'…';
  try{
    const body=new URLSearchParams({pin,mode});
    const r=await fetch('/api/receiver/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
    const d=await r.json();
    msg.textContent=d.message||'Controllo aggiornato';
    updateReceiver();
  }catch(e){msg.textContent='Errore durante la modifica del ricevitore'}
}

async function setOutMode(mode){
  const msg=document.getElementById('receiverMsg');
  msg.textContent='Impostazione OUT '+mode.toUpperCase()+'…';
  try{
    const body=new URLSearchParams({mode});
    const r=await fetch('/api/receiver/out',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
    const d=await r.json();
    msg.textContent=d.message||'OUT aggiornato';
    updateReceiver();
  }catch(e){msg.textContent='Errore durante la modifica di OUT'}
}

async function setPolarity(mode){
  const msg=document.getElementById('receiverMsg');
  msg.textContent='Impostazione polarità OUT…';
  try{
    const body=new URLSearchParams({mode});
    const r=await fetch('/api/receiver/polarity',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
    const d=await r.json();
    msg.textContent=d.message||'Polarità aggiornata';
    updateReceiver();
  }catch(e){msg.textContent='Errore durante la modifica della polarità'}
}

async function startPon(){
  const msg=document.getElementById('receiverMsg'),btn=document.getElementById('ponStartBtn');
  btn.disabled=true;msg.textContent='START PON: HIGH per 3 secondi, poi LOW…';
  try{
    const r=await fetch('/api/receiver/pon-start',{method:'POST'});
    const d=await r.json();
    msg.textContent=d.message||'Sequenza PON avviata';
    setTimeout(()=>{btn.disabled=false;updateReceiver()},3400);
  }catch(e){btn.disabled=false;msg.textContent='Errore durante START PON'}
}

async function updateScope(){
  if(Date.now()<quietUntil)return;
  if(document.hidden)return;
  try{
    const r=await fetch('/api/scope',{cache:'no-store'});
    if(!r.ok)throw Error();
    const d=await r.json();
    document.getElementById('scopeLine').textContent=d.ready?d.line:'Attesa primo secondo completo…';
    if(d.protocol==='MSF'){document.getElementById('scopeInfo').textContent='MSF 60 kHz · '+d.samples+' campioni · fase '+(d.phaseBin*10)+' ms · '+(d.phaseLocked?'PHASE LOCK':'ricerca fase')+' · '+d.lastSymbol+' · drop '+d.droppedWindows;return}
    document.getElementById('scopeInfo').textContent=d.ready
      ? ('attivo '+d.activeMs+' ms/finestra · campioni '+d.samples+' · durata '+d.windowDurationMs+' ms · frequenza '+d.sampleRateHz+' Hz · fase '+d.phaseBin+'0 ms · qualità fase '+d.phaseQuality+'% · '+(d.phaseLocked?'PHASE LOCK':'ricerca fase')+' · simbolo '+d.lastSymbol+' ('+d.lastConfidence+'%) · secondo '+(d.secondLocked?d.secondIndex:'?')+' · qualità minuto '+d.secondQuality+' · candidato59 '+d.minuteBestCandidate+' · score '+d.minuteScoreMax+'/'+d.minuteScoreNoise+' · delta '+d.secondQuality+'/'+d.minuteLockThreshold+' · RAW fronti '+d.rawRisingEdges+' · blocchi≥30ms '+d.rawLongBlocks+' · max blocco '+d.rawLongestBlockMs+' ms · filtrati '+d.filteredRisingEdges+' fronti / '+d.filteredLongBlocks+' blocchi / max '+d.filteredLongestBlockMs+' ms · finestre scartate '+d.rejectedWindows+' · SYNC? grezzi '+d.rawSyncCandidates+' · confermati '+d.syncCandidates+' · '+(d.secondLocked?'MINUTE LOCK':'accumulo minuto')+' · drop '+d.droppedWindows)
      : '';
  }catch(e){
    document.getElementById('scopeLine').textContent='Errore lettura scope';
  }
}

async function updatePulses(){
  if(Date.now()<quietUntil)return;
  if(document.hidden)return;
  try{
    const r=await fetch('/api/pulses',{cache:'no-store'});
    if(!r.ok)throw Error();
    const d=await r.json(),body=document.getElementById('pulseRows');
    body.replaceChildren();
    if(!d.pulses.length){
      const tr=document.createElement('tr'),td=document.createElement('td');
      td.colSpan=9;td.textContent='Attesa simboli / impulsi…';tr.append(td);body.append(tr);return;
    }
    d.pulses.forEach(p=>{
      const tr=document.createElement('tr');
      const vals=[
        (p.ageMs/1000).toFixed(1)+' s',
        p.source,
        p.secondIndex===null?'?':p.secondIndex,
        p.widthMs.toFixed(3),
        p.periodMs.toFixed(3),
        p.bitLabel,
        p.confidence+'%',
        p.timingLabel,
        p.framePos===null?'?':p.framePos
      ];
      vals.forEach((v,i)=>{const td=document.createElement('td');td.textContent=v;tr.append(td)});
      tr.className=p.valid&&p.secondTimingOk?'good':(p.glitch?'bad':'neutral');
      body.append(tr);
    });
  }catch(e){
    const body=document.getElementById('pulseRows');body.innerHTML='<tr><td colspan="9">Errore lettura simboli / impulsi</td></tr>';
  }
}

async function scanNetworks(){
  const b=document.getElementById('scanBtn'), msg=document.getElementById('wifiMsg'), sel=document.getElementById('ssid');
  b.disabled=true;msg.textContent='Scansione in corso…';
  try{
    const r=await fetch('/api/networks',{cache:'no-store'});
    if(!r.ok)throw Error();
    const d=await r.json();
    sel.replaceChildren();
    if(!d.networks.length){const o=document.createElement('option');o.value='';o.textContent='Nessuna rete trovata';sel.append(o)}
    else d.networks.forEach(n=>{const o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+' dBm'+(n.open?', aperta':'')+')';sel.append(o)});
    msg.textContent=d.networks.length+' reti rilevate';
  }catch(e){msg.textContent='Errore durante la scansione Wi-Fi'}
  b.disabled=false;
}

async function connectWifi(){
  const ssid=document.getElementById('ssid').value, pass=document.getElementById('pass').value;
  const b=document.getElementById('connectBtn'),msg=document.getElementById('wifiMsg');
  if(!ssid){msg.textContent='Seleziona prima una rete Wi-Fi';return}
  b.disabled=true;msg.textContent='Avvio connessione a '+ssid+'…';
  try{
    const body=new URLSearchParams({ssid,password:pass});
    const r=await fetch('/api/wifi/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
    const d=await r.json();
    msg.textContent=d.message||'Connessione avviata';
    document.getElementById('pass').value='';
    setTimeout(updateWifi,1500);
  }catch(e){msg.textContent='Errore nell\'avvio della connessione'}
  b.disabled=false;
}

setInterval(update,2000);
setInterval(updateWifi,3000);
setInterval(updateReceiver,3000);
setInterval(updatePulses,2000);
setInterval(updateScope,1000);
document.addEventListener('visibilitychange',()=>{update();updateWifi();updateReceiver();updatePulses();updateScope()});
update();updateWifi();updateReceiver();updatePulses();updateScope();
</script></html>)HTML";

void status() {
    if (!currentDecoder) { server.send(503,"application/json","{}"); return; }
    const auto &s = currentDecoder->stats();
    DCFDateTime dt;
    const bool clock = currentDecoder->getRunningClock(dt);
    String json; json.reserve(1400);
    json = "{\"clockAvailable\":"; json += clock ? "true" : "false";
    SampledDcfSnapshot snap;
    sampledDcfSnapshot(snap);
    json += ",\"signalRecent\":";
    json += (snap.rawTransitions && snap.rawTransitionAgeMs < 3500) ? "true" : "false";
    json += ",\"decodedSignalRecent\":";
    json += (s.totalPulses && millis()-lastPulseMs < 3500) ? "true" : "false";
    json += ",\"phaseLocked\":"; json += (selectedSignalMode==SignalMode::MSF_60KHZ ? s.msfPhaseLocked : snap.phaseLocked) ? "true" : "false";
    json += ",\"secondLocked\":"; json += (selectedSignalMode==SignalMode::MSF_60KHZ ? s.minuteSynced : snap.secondLocked) ? "true" : "false";
    char time[16] = "", date[32] = "";
    if (clock) {
        snprintf(time,sizeof(time),"%02d:%02d:%02d",dt.hour,dt.minute,dt.second);
        snprintf(date,sizeof(date),"%02d/%02d/%04d %s",dt.day,dt.month,dt.year,selectedSignalMode==SignalMode::MSF_60KHZ ? (dt.cest?"BST":"GMT") : (dt.cest?"CEST":"CET"));
    }
    json += ",\"time\":\""; json += time; json += "\",\"date\":\""; json += date; json += "\"";
    auto number = [&](const char *key, double value) { json += ",\""; json += key; json += "\":"; json += String(value,3); };
    json += ",\"acquisitionState\":\""; json += currentDecoder->acquisitionState(); json += "\"";
    number("acquisitionConfidence",s.acquisitionConfidence);
    number("fieldConfidence",s.fieldConfidence);
    number("predictionMatch",s.predictionMatch);
    number("sampledSymbols",s.sampledSymbols);
    json += ",\"protocol\":\""; json += selectedSignalMode==SignalMode::MSF_60KHZ ? "MSF" : selectedSignalMode==SignalMode::DCF77 ? "DCF77" : "RAW"; json += "\"";
    number("msfBitA",s.msfBitA); number("msfBitB",s.msfBitB); number("msfPhaseBin",s.msfPhaseBin);
    number("rawTransitions",snap.rawTransitions);
    number("candidateMinutes",s.candidateMinutes);
    number("recoveredBits",s.recoveredBits);
    number("uncertainBits",s.uncertainBits);
    const bool qualityRecent = s.totalPulses && millis()-lastPulseMs < 3500 &&
        (selectedSignalMode==SignalMode::MSF_60KHZ ? s.msfPhaseLocked : selectedSignalMode != SignalMode::DCF77 || snap.phaseLocked);
    number("historicalQuality",s.quality);
    json += ",\"quality\":"; json += qualityRecent ? String(s.quality) : String("null");
    json += ",\"qualityRecent\":"; json += qualityRecent ? "true" : "false";
    json += ",\"minuteSynced\":"; json += s.minuteSynced ? "true" : "false";
    number("minuteMarkers",s.minuteMarkers);
    number("frameBitCount",s.frameBitCount); number("lastBit",s.lastBit);
    number("pulseMs",s.lastPulseWidthUs/1000.0); number("periodMs",s.lastPeriodUs/1000.0);
    number("jitterMs",s.lastJitterUs/1000.0); number("rmsMs",s.jitterRmsUs/1000.0);
    number("validPulses",s.validPulses); number("invalidPulses",s.invalidPulses);
    number("validFrames",s.validFrames); number("invalidFrames",s.invalidFrames);
    number("parityErrors",s.parityErrors); number("timingErrors",s.timingErrors); number("glitches",s.glitchCount);
    number("freeHeap",ESP.getFreeHeap());
    json += ",\"frameAgeSeconds\":";
    json += s.validFrames ? String((millis()-s.lastValidFrameMs)/1000) : String("null");
    json += ",\"ppsUs\":"; json += s.lastPpsOffsetUs == INT32_MIN ? String("null") : String(s.lastPpsOffsetUs);
    json += ",\"liveFrame\":\"";
    const int8_t *liveBits = currentDecoder->currentFrameBits();
    for (uint8_t i=0;i<currentDecoder->currentFrameCount();++i) json += liveBits[i]<0?'?':(liveBits[i]?'1':'0');
    json += "\",\"frame\":\"";
    const int8_t *bits = currentDecoder->lastFrameBits();
    for (uint8_t i=0;i<currentDecoder->lastFrameCount();++i) json += bits[i]<0?'?':(bits[i]?'1':'0');
    json += "\"}";
    server.sendHeader("Cache-Control","no-store");
    server.send(200,"application/json",json);
}

void scopeStatus() {
    SampledDcfSnapshot snap;
    sampledDcfSnapshot(snap);
    const bool msf=selectedSignalMode==SignalMode::MSF_60KHZ && currentDecoder;
    const DecoderStats *msfStats=msf ? &currentDecoder->stats() : nullptr;

    String json;
    json.reserve(420);
    json = "{\"protocol\":\""; json += msf ? "MSF" : "DCF77"; json += "\",\"ready\":";
    json += snap.ready ? "true" : "false";
    json += ",\"line\":\"";
    if (snap.ready) {
        for (uint8_t i = 0; i < 100; ++i) json += scopeChar(snap.bins[i]);
    }
    json += "\",\"samples\":";
    json += String(snap.samples);
    json += ",\"windowDurationMs\":";
    json += String(snap.windowDurationUs / 1000.0f, 2);
    json += ",\"sampleRateHz\":";
    json += snap.windowDurationUs ? String(snap.samples * 1000000.0f / snap.windowDurationUs, 1) : String("null");
    json += ",\"activeMs\":";
    json += String(snap.activeMs);
    json += ",\"coverage\":";
    json += String(snap.samples >= 1000 ? 100 : (snap.samples * 100UL) / 1000UL);
    json += ",\"phaseBin\":";
    json += String(msf ? msfStats->msfPhaseBin : snap.phaseBin);
    json += ",\"phaseQuality\":";
    json += String(msf ? msfStats->quality : snap.phaseQuality);
    json += ",\"phaseLocked\":";
    json += (msf ? msfStats->msfPhaseLocked : snap.phaseLocked) ? "true" : "false";
    json += ",\"lastSymbol\":\"";
    if(msf) { json += "A"; json += String(msfStats->msfBitA); json += "/B"; json += String(msfStats->msfBitB); }
    else if (snap.lastMinuteMarker) json += "MIN";
    else if (snap.lastBit == 0) json += "0";
    else if (snap.lastBit == 1) json += "1";
    else json += "?";
    json += "\",\"lastConfidence\":";
    json += String(snap.lastConfidence);
    json += ",\"lastPulseMs\":";
    json += String(snap.lastPulseMs);
    json += ",\"secondsObserved\":";
    json += String(snap.secondsObserved);
    json += ",\"droppedWindows\":";
    json += String(snap.droppedWindows);
    json += ",\"secondIndex\":";
    json += snap.secondLocked ? String(snap.secondIndex) : String("null");
    json += ",\"secondQuality\":";
    json += String(snap.secondQuality);
    json += ",\"secondLocked\":";
    json += (msf ? msfStats->minuteSynced : snap.secondLocked) ? "true" : "false";
    json += ",\"minuteBestCandidate\":";
    json += String(snap.minuteBestCandidate);
    json += ",\"minuteScoreMax\":";
    json += String(snap.minuteScoreMax);
    json += ",\"minuteScoreNoise\":";
    json += String(snap.minuteScoreNoise);
    json += ",\"minuteLockThreshold\":";
    json += String(snap.minuteLockThreshold);
    json += ",\"syncCandidates\":";
    json += String(snap.syncCandidates);
    json += ",\"rawSyncCandidates\":";
    json += String(snap.rawSyncCandidates);
    json += ",\"rawRisingEdges\":";
    json += String(snap.rawRisingEdges);
    json += ",\"rawLongBlocks\":";
    json += String(snap.rawLongBlocks);
    json += ",\"rawLongestBlockMs\":";
    json += String(snap.rawLongestBlockMs);
    json += ",\"rawTransitions\":"; json += String(snap.rawTransitions);
    json += ",\"rawTransitionAgeMs\":";
    json += snap.rawTransitions ? String(snap.rawTransitionAgeMs) : String("null");
    json += ",\"rejectedWindows\":"; json += String(snap.rejectedWindows);
    json += ",\"filteredRisingEdges\":"; json += String(snap.filteredRisingEdges);
    json += ",\"filteredLongBlocks\":"; json += String(snap.filteredLongBlocks);
    json += ",\"filteredLongestBlockMs\":"; json += String(snap.filteredLongestBlockMs);
    json += "}";
    server.sendHeader("Cache-Control","no-store");
    server.send(200,"application/json",json);
}

void pulseStatus() {
    if (!currentDecoder) { server.send(503,"application/json","{}"); return; }
    String json;
    json.reserve(3600);
    json = "{\"pulses\":[";
    const uint8_t count = currentDecoder->recentPulseCount();
    const uint32_t now = millis();
    for (uint8_t i = 0; i < count; ++i) {
        PulseTrace p;
        if (!currentDecoder->recentPulse(i, p)) continue;
        if (i) json += ',';
        const bool glitch = p.widthUs < 30000;
        json += "{\"ageMs\":"; json += String(now - p.capturedMs);
        json += ",\"widthMs\":"; json += String(p.widthUs / 1000.0, 3);
        json += ",\"periodMs\":"; json += String(p.periodUs / 1000.0, 3);
        json += ",\"bitLabel\":\"";
        if (p.minuteGap) json += "MIN";
        else if (p.markerCandidate) json += "SYNC?";
        else if (p.bit == 0) json += "0";
        else if (p.bit == 1) json += "1";
        else json += "?";
        json += "\",\"valid\":"; json += p.valid ? "true" : "false";
        json += ",\"glitch\":"; json += glitch ? "true" : "false";
        json += ",\"secondTimingOk\":"; json += p.secondTimingOk ? "true" : "false";
        json += ",\"timingLabel\":\"";
        if (p.minuteGap) json += "MIN";
        else if (p.secondTimingOk) json += "1s OK";
        else json += "fuori";
        json += "\",\"framePos\":";
        json += p.secondIndex == 255 ? String("null") : String(p.framePos);
        json += ",\"source\":\""; json += p.sampled ? "sampled" : "edge"; json += "\"";
        json += ",\"confidence\":"; json += String(p.confidence);
        json += ",\"secondIndex\":";
        json += p.secondIndex == 255 ? String("null") : String(p.secondIndex);
        json += "}";
    }
    json += "]}";
    server.sendHeader("Cache-Control","no-store");
    server.send(200,"application/json",json);
}

void receiverStatus() {
    String json;
    json.reserve(320);
    json = "{\"sel\":\"";
    json += modeLabel(selMode);
    json += "\",\"decoderMode\":\"";
    json += selectedSignalMode == SignalMode::DCF77 ? "DCF77 77.5 kHz" : selectedSignalMode==SignalMode::MSF_60KHZ ? "MSF UK 60 kHz" : "RAW 60 kHz";
    json += "\",\"selGpio\":";
    json += String(PIN_RX_BAND);
    json += ",\"pon\":\"";
    json += modeLabel(ponMode);
    json += "\",\"ponGpio\":";
    json += String(PIN_RX_PON);
    json += ",\"outLevel\":\"";
    json += digitalRead(PIN_DCF77) ? "HIGH" : "LOW";
    json += "\",\"outGpio\":";
    json += String(PIN_DCF77);
    json += ",\"outMode\":\"";
    json += outPullupEnabled ? "INPUT_PULLUP" : "INPUT";
    json += "\",\"polarity\":\"";
    json += dcfActiveLowSelected ? "LOW" : "HIGH";
    json += "\",\"ponStartActive\":";
    json += ponStartActive ? "true" : "false";
    json += ",\"ponStartRemainingMs\":";
    const uint32_t elapsed = ponStartActive ? (millis() - ponStartBeganMs) : 0;
    json += ponStartActive && elapsed < PON_START_HIGH_MS
              ? String(PON_START_HIGH_MS - elapsed)
              : String(0);
    json += ",\"eventsPerSecond\":";
    json += String(pulseEventsPerSecond,1);
    json += ",\"eventSource\":\"raw_transitions\"";
    json += ",\"secondsSinceChange\":";
    json += receiverChangedMs ? String((millis() - receiverChangedMs) / 1000UL) : String(0);
    json += "}";
    server.sendHeader("Cache-Control","no-store");
    server.send(200,"application/json",json);
}

bool parseDriveMode(const String &mode, PinDriveMode &out) {
    if (mode == "float") out = PinDriveMode::FLOATING;
    else if (mode == "low") out = PinDriveMode::LOW_LEVEL;
    else if (mode == "high") out = PinDriveMode::HIGH_LEVEL;
    else return false;
    return true;
}

void setDecoderMode() {
    if (rejectDuringRecording()) return;
    if (!server.hasArg("mode")) {
        server.send(400,"application/json","{\"ok\":false,\"message\":\"Parametro mode mancante\"}");
        return;
    }
    const String mode = server.arg("mode");
    if (mode == "dcf77") selectedSignalMode = SignalMode::DCF77;
    else if (mode == "raw60") selectedSignalMode = SignalMode::RAW_60KHZ;
    else if (mode == "msf60") selectedSignalMode = SignalMode::MSF_60KHZ;
    else {
        server.send(400,"application/json","{\"ok\":false,\"message\":\"Decoder non valido\"}");
        return;
    }
    resetReceiverDiagnostics();
    server.send(200,"application/json",
        selectedSignalMode == SignalMode::DCF77
          ? "{\"ok\":true,\"message\":\"Decoder DCF77 77,5 kHz selezionato\"}"
          : selectedSignalMode==SignalMode::MSF_60KHZ
          ? "{\"ok\":true,\"message\":\"Decoder MSF UK 60 kHz selezionato; SEL va verificato separatamente\"}"
          : "{\"ok\":true,\"message\":\"Analizzatore RAW 60 kHz selezionato\"}");
}

void setReceiverControl() {
    if (rejectDuringRecording()) return;
    if (!server.hasArg("pin") || !server.hasArg("mode")) {
        server.send(400,"application/json","{\"ok\":false,\"message\":\"Parametri pin/mode mancanti\"}");
        return;
    }
    const String pin = server.arg("pin");
    PinDriveMode mode;
    if (!parseDriveMode(server.arg("mode"), mode)) {
        server.send(400,"application/json","{\"ok\":false,\"message\":\"Valore mode non valido\"}");
        return;
    }
    if (pin == "sel") applySelMode(mode);
    else if (pin == "pon") applyPonMode(mode);
    else {
        server.send(400,"application/json","{\"ok\":false,\"message\":\"Pin non valido\"}");
        return;
    }
    resetReceiverDiagnostics();

    String msg = "{\"ok\":true,\"message\":\"";
    msg += pin == "sel" ? "SEL" : "PON";
    msg += " aggiornato a ";
    msg += modeLabel(mode);
    msg += "\"}";
    server.send(200,"application/json",msg);
}

void setOutMode() {
    if (rejectDuringRecording()) return;
    if (!server.hasArg("mode")) {
        server.send(400,"application/json","{\"ok\":false,\"message\":\"Parametro mode mancante\"}");
        return;
    }
    const String mode = server.arg("mode");
    if (mode == "input") applyOutInputMode(false);
    else if (mode == "pullup") applyOutInputMode(true);
    else {
        server.send(400,"application/json","{\"ok\":false,\"message\":\"Modalità OUT non valida\"}");
        return;
    }
    server.send(200,"application/json",
        mode == "input"
          ? "{\"ok\":true,\"message\":\"OUT impostato su INPUT senza pull-up\"}"
          : "{\"ok\":true,\"message\":\"OUT impostato su INPUT_PULLUP\"}");
}

void setPolarity() {
    if (rejectDuringRecording()) return;
    if (!server.hasArg("mode")) {
        server.send(400,"application/json","{\"ok\":false,\"message\":\"Parametro mode mancante\"}");
        return;
    }
    const String mode = server.arg("mode");
    if (mode == "low") dcfActiveLowSelected = true;
    else if (mode == "high") dcfActiveLowSelected = false;
    else {
        server.send(400,"application/json","{\"ok\":false,\"message\":\"Polarità non valida\"}");
        return;
    }
    resetReceiverDiagnostics();
    server.send(200,"application/json",
        dcfActiveLowSelected
          ? "{\"ok\":true,\"message\":\"OUT impostato ACTIVE LOW\"}"
          : "{\"ok\":true,\"message\":\"OUT impostato ACTIVE HIGH\"}");
}

void startPonSequence() {
    if (rejectDuringRecording()) return;
    applyPonMode(PinDriveMode::HIGH_LEVEL);
    ponStartBeganMs = millis();
    ponStartActive = true;
    resetReceiverDiagnostics(false);
    Serial.println("MASO START PON: HIGH, waiting 3000 ms before LOW");
    server.send(202,"application/json","{\"ok\":true,\"message\":\"START PON avviato: HIGH per 3 s, poi LOW automatico\"}");
}

void wifiStatus() {
    const wl_status_t st = WiFi.status();
    String json; json.reserve(300);
    json = "{\"connected\":";
    json += st == WL_CONNECTED ? "true" : "false";
    json += ",\"connecting\":";
    json += (st == WL_IDLE_STATUS) ? "true" : "false";
    json += ",\"ssid\":\"";
    json += jsonEscape(st == WL_CONNECTED ? WiFi.SSID() : String(""));
    json += "\",\"ip\":\"";
    json += st == WL_CONNECTED ? WiFi.localIP().toString() : String("");
    json += "\",\"rssi\":";
    json += st == WL_CONNECTED ? String(WiFi.RSSI()) : String("null");
    json += "}";
    server.sendHeader("Cache-Control","no-store");
    server.send(200,"application/json",json);
}

void scanNetworks() {
    if (rejectDuringRecording()) return;
    const int count = WiFi.scanNetworks(false, true);
    String json;
    json.reserve(180 + (count > 0 ? count * 80 : 0));
    json = "{\"networks\":[";
    bool first = true;
    for (int i = 0; i < count; ++i) {
        const String ssid = WiFi.SSID(i);
        if (!ssid.length()) continue;
        if (!first) json += ',';
        first = false;
        json += "{\"ssid\":\"";
        json += jsonEscape(ssid);
        json += "\",\"rssi\":";
        json += String(WiFi.RSSI(i));
        json += ",\"open\":";
        json += WiFi.encryptionType(i) == ENC_TYPE_NONE ? "true" : "false";
        json += "}";
    }
    json += "]}";
    WiFi.scanDelete();
    server.sendHeader("Cache-Control","no-store");
    server.send(200,"application/json",json);
}

void connectWifi() {
    if (rejectDuringRecording()) return;
    if (!server.hasArg("ssid")) {
        server.send(400,"application/json","{\"ok\":false,\"message\":\"SSID mancante\"}");
        return;
    }

    const String ssid = server.arg("ssid");
    const String password = server.arg("password");
    if (!ssid.length() || ssid.length() > 32 || password.length() > 63) {
        server.send(400,"application/json","{\"ok\":false,\"message\":\"Parametri Wi-Fi non validi\"}");
        return;
    }

    WiFi.persistent(true);
    if (password.length()) WiFi.begin(ssid.c_str(), password.c_str());
    else WiFi.begin(ssid.c_str());
    WiFi.persistent(false);

    Serial.printf("WiFi STA: connection requested for SSID '%s'\n", ssid.c_str());
    server.send(202,"application/json","{\"ok\":true,\"message\":\"Connessione avviata. Lo stato si aggiorna automaticamente.\"}");
}
}

void portalBegin() {
    selMode = PinDriveMode::FLOATING;
    ponMode = PinDriveMode::LOW_LEVEL;
    outPullupEnabled = false;
    selectedSignalMode = SignalMode::MSF_60KHZ;
    dcfActiveLowSelected = DCF77_ACTIVE_LOW;
    resetScope();

    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);

    char ssid[32];
    snprintf(ssid,sizeof(ssid),"DCF77-HW364A-%06X",ESP.getChipId());

    // Open local configuration AP. Keep it available even when STA is connected.
    if (!WiFi.softAP(ssid)) Serial.println("WiFi AP startup failed");
    Serial.printf("WiFi AP: %s; open network; http://192.168.4.1\n",ssid);

    // Reconnect using credentials previously stored by an explicit portal request.
    WiFi.begin();

    server.on("/",HTTP_GET,[](){server.send_P(200,"text/html; charset=utf-8",page);});
    server.on("/api/status",HTTP_GET,status);
    server.on("/api/pulses",HTTP_GET,pulseStatus);
    server.on("/api/scope",HTTP_GET,scopeStatus);
    server.on("/api/receiver",HTTP_GET,receiverStatus);
    server.on("/api/decoder",HTTP_POST,setDecoderMode);
    server.on("/api/receiver/control",HTTP_POST,setReceiverControl);
    server.on("/api/receiver/out",HTTP_POST,setOutMode);
    server.on("/api/receiver/polarity",HTTP_POST,setPolarity);
    server.on("/api/receiver/pon-start",HTTP_POST,startPonSequence);
    server.on("/api/wifi",HTTP_GET,wifiStatus);
    server.on("/api/networks",HTTP_GET,scanNetworks);
    server.on("/api/wifi/connect",HTTP_POST,connectWifi);
    server.on("/api/recording",HTTP_GET,recordingStatus);
    server.on("/api/recording/start",HTTP_POST,startRecording);
    server.on("/api/recording/stop",HTTP_POST,stopRecording);
    server.on("/api/recording/download",HTTP_GET,downloadRecording);
    server.onNotFound([](){server.send(404,"text/plain","Not found");});
    server.begin();
}

void portalPoll(const DCF77Decoder &decoder, const ReceiverControl &receiver) {
    (void)receiver;
    currentDecoder=&decoder;
    pollScope();
    const uint32_t now = millis();
    const uint32_t total = decoder.stats().totalPulses;
    SampledDcfSnapshot snap;
    sampledDcfSnapshot(snap);
    if (observedPulses != total) {
        observedPulses = total;
        lastPulseMs = now;
    }
    if (ponStartActive && now - ponStartBeganMs >= PON_START_HIGH_MS) {
        ponStartActive = false;
        applyPonMode(PinDriveMode::LOW_LEVEL);
        resetReceiverDiagnostics(false);
        Serial.println("MASO START PON: transition HIGH -> LOW completed; diagnostics reset");
    }

    if (!rateSampleMs) {
        rateSampleMs = now;
        rateSamplePulses = snap.rawTransitions;
    } else if (now - rateSampleMs >= 1000) {
        const uint32_t elapsed = now - rateSampleMs;
        pulseEventsPerSecond = (snap.rawTransitions - rateSamplePulses) * 1000.0f / elapsed;
        rateSampleMs = now;
        rateSamplePulses = snap.rawTransitions;
    }
    if (!radioPaused) server.handleClient();
}

void portalRecordingPoll(AnalyzerUI &ui) {
    if (!recordBusy) return;
    const uint32_t now = millis();
    if (recordStage == 1 && now-recordStageMs >= 1500) {
        if (recordQuiet) {
            ui.suspend(true);
            WiFi.persistent(false);
            WiFi.mode(WIFI_OFF);
            radioPaused = true;
        }
        recordStage=2; recordStageMs=now;
    }
    if (recordStage == 2 && now-recordStageMs >= 2000) {
        rawRecordStart(); recordStartedMs=now; recordStage=3;
    }
    if (recordStage == 3 && (!rawRecordRunning() || now-recordStartedMs >= 190000)) {
        if (rawRecordRunning()) { recordCancelled=true; rawRecordStop(); }
        recordStage=4;
    }
    if (recordStage == 4) {
        rawRecordStop();
        if (radioPaused) {
            WiFi.mode(WIFI_AP_STA);
            char ssid[32]; snprintf(ssid,sizeof(ssid),"DCF77-HW364A-%06X",ESP.getChipId());
            WiFi.softAP(ssid);
            WiFi.begin(); // existing stored STA credentials; no credential writes
            radioPaused=false;
            ui.suspend(false);
        }
        recordBusy=false; recordStage=0;
        Serial.printf("Recording ended: %lu samples, gaps=%lu; WiFi/OLED restored\n",
            (unsigned long)rawRecordCount(),(unsigned long)rawRecordTimingGaps());
    }
}
bool portalRecordingBusy() { return recordBusy; }
void portalRecordingCancel() {
    if (recordBusy) { recordCancelled=true; rawRecordStop(); recordStage=4; }
}

bool portalTakeReceiverResetRequest() {
    const bool requested = receiverResetRequested;
    receiverResetRequested = false;
    return requested;
}

bool portalDcfActiveLow() {
    return dcfActiveLowSelected;
}

SignalMode portalSignalMode() {
    return selectedSignalMode;
}

const char *portalAddress() { return "192.168.4.1"; }
#else
void portalRecordingPoll(AnalyzerUI &) {}
bool portalRecordingBusy() { return false; }
void portalRecordingCancel() {}
void portalBegin() {}
void portalPoll(const DCF77Decoder &, const ReceiverControl &) {}
bool portalTakeReceiverResetRequest() { return false; }
bool portalDcfActiveLow() { return DCF77_ACTIVE_LOW; }
SignalMode portalSignalMode() { return SignalMode::DCF77; }
const char *portalAddress() { return ""; }
#endif
