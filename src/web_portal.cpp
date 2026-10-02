#include "web_portal.h"
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "sampled_dcf77.h"

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
SignalMode selectedSignalMode = SignalMode::DCF77;

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
<title>DCF77 HW-364A</title>
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
<h1>DCF77 · HW-364A</h1>
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

<h2>Scope DCF77 · 1 secondo</h2>
<p class="muted">Campionamento hardware di OUT a 1 kHz, indipendente da Wi-Fi/web/OLED. 100 celle da 10 ms: "-" = inattivo, 1..9 = attività parziale, X = attivo per tutta la cella. La stessa acquisizione alimenta il decoder DCF77 principale.</p>
<pre id="scopeLine">Attesa primo secondo completo…</pre>
<p id="scopeInfo" class="muted"></p>

<h2>Monitor simboli / impulsi</h2>
<p class="muted">In modalità DCF77 mostra i simboli prodotti dal decoder campionato (secondo, bit e confidenza). In RAW 60 kHz continua a mostrare gli impulsi grezzi.</p>
<table>
<thead><tr><th>Età</th><th>Sorgente</th><th>Secondo</th><th>Impulso ms</th><th>Periodo ms</th><th>Bit</th><th>Conf.</th><th>Timing</th><th>Frame</th></tr></thead>
<tbody id="pulseRows"><tr><td colspan="6">Attesa impulsi…</td></tr></tbody>
</table>

<h2>Frame corrente</h2><pre id="liveFrame">—</pre>
<h2>Ultimo frame completato</h2><pre id="frame">—</pre>

<script>
const labels={acquisitionState:'Stato acquisizione',acquisitionConfidence:'Confidenza acquisizione (%)',fieldConfidence:'Confidenza campi BCD (%)',predictionMatch:'Coerenza predittiva (%)',sampledSymbols:'Simboli campionati',candidateMinutes:'Minuti coerenti',recoveredBits:'Bit recuperati',uncertainBits:'Bit incerti',quality:'Qualità temporale (%)',minuteSynced:'Sincronizzazione minuto',minuteMarkers:'Marker minuto rilevati',frameBitCount:'Posizione frame',lastBit:'Ultimo bit',pulseMs:'Impulso (ms)',periodMs:'Periodo (ms)',jitterMs:'Jitter (ms)',rmsMs:'Jitter RMS (ms)',validPulses:'Impulsi validi',invalidPulses:'Impulsi invalidi',validFrames:'Frame validi',invalidFrames:'Frame invalidi',parityErrors:'Errori parità',timingErrors:'Errori temporali',glitches:'Glitch',frameAgeSeconds:'Età ultimo frame (s)',ppsUs:'Offset PPS (µs)',freeHeap:'RAM libera (byte)'};

async function update(){
  if(document.hidden)return;
  try{
    const r=await fetch('/api/status',{cache:'no-store'});
    if(!r.ok)throw Error();
    const d=await r.json();
    document.getElementById('clock').textContent=d.time||'--:--:--';
    document.getElementById('date').textContent=d.date||'Attesa frame valido';
    document.getElementById('status').textContent=d.signalRecent?(d.clockAvailable?'Segnale presente · orologio disponibile':'Segnale presente · acquisizione'):'Segnale assente o non ancora ricevuto';
    const list=document.getElementById('metrics');list.replaceChildren();
    for(const [k,l]of Object.entries(labels)){const a=document.createElement('dt'),b=document.createElement('dd');a.textContent=l;b.textContent=k==='minuteSynced'?(d[k]?'AGGANCIATO':'IN ATTESA'):(d[k]??'—');list.append(a,b)}
    document.getElementById('liveFrame').textContent=d.liveFrame||'Nessun frame corrente';
    document.getElementById('frame').textContent=d.frame||'Nessun frame completato';
  }catch(e){document.getElementById('status').textContent='Connessione al dispositivo persa'}
}

async function updateWifi(){
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
      ' | eventi '+d.eventsPerSecond+'/s'+
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
  if(document.hidden)return;
  try{
    const r=await fetch('/api/scope',{cache:'no-store'});
    if(!r.ok)throw Error();
    const d=await r.json();
    document.getElementById('scopeLine').textContent=d.ready?d.line:'Attesa primo secondo completo…';
    document.getElementById('scopeInfo').textContent=d.ready
      ? ('attivo '+d.activeMs+' ms/s · campioni '+d.samples+'/1000 · copertura '+d.coverage+'% · fase '+d.phaseBin+'0 ms · qualità fase '+d.phaseQuality+'% · '+(d.phaseLocked?'PHASE LOCK':'ricerca fase')+' · simbolo '+d.lastSymbol+' ('+d.lastConfidence+'%) · secondo '+(d.secondLocked?d.secondIndex:'?')+' · qualità minuto '+d.secondQuality+' · '+(d.secondLocked?'MINUTE LOCK':'accumulo minuto')+' · drop '+d.droppedWindows)
      : '';
  }catch(e){
    document.getElementById('scopeLine').textContent='Errore lettura scope';
  }
}

async function updatePulses(){
  if(document.hidden)return;
  try{
    const r=await fetch('/api/pulses',{cache:'no-store'});
    if(!r.ok)throw Error();
    const d=await r.json(),body=document.getElementById('pulseRows');
    body.replaceChildren();
    if(!d.pulses.length){
      const tr=document.createElement('tr'),td=document.createElement('td');
      td.colSpan=6;td.textContent='Attesa impulsi…';tr.append(td);body.append(tr);return;
    }
    d.pulses.forEach(p=>{
      const tr=document.createElement('tr');
      const vals=[
        (p.ageMs/1000).toFixed(1)+' s',
        p.widthMs.toFixed(3),
        p.periodMs.toFixed(3),
        p.bitLabel,
        p.timingLabel,
        p.framePos
      ];
      vals.forEach((v,i)=>{const td=document.createElement('td');td.textContent=v;tr.append(td)});
      tr.className=p.valid&&p.secondTimingOk?'good':(p.glitch?'bad':'neutral');
      body.append(tr);
    });
  }catch(e){
    const body=document.getElementById('pulseRows');body.innerHTML='<tr><td colspan="6">Errore lettura impulsi</td></tr>';
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
    json += ",\"signalRecent\":";
    json += (s.totalPulses && millis()-lastPulseMs < 3500) ? "true" : "false";
    char time[16] = "", date[32] = "";
    if (clock) {
        snprintf(time,sizeof(time),"%02d:%02d:%02d",dt.hour,dt.minute,dt.second);
        snprintf(date,sizeof(date),"%02d/%02d/%04d %s",dt.day,dt.month,dt.year,dt.cest?"CEST":"CET");
    }
    json += ",\"time\":\""; json += time; json += "\",\"date\":\""; json += date; json += "\"";
    auto number = [&](const char *key, double value) { json += ",\""; json += key; json += "\":"; json += String(value,3); };
    json += ",\"acquisitionState\":\""; json += currentDecoder->acquisitionState(); json += "\"";
    number("acquisitionConfidence",s.acquisitionConfidence);
    number("fieldConfidence",s.fieldConfidence);
    number("predictionMatch",s.predictionMatch);
    number("sampledSymbols",s.sampledSymbols);
    number("candidateMinutes",s.candidateMinutes);
    number("recoveredBits",s.recoveredBits);
    number("uncertainBits",s.uncertainBits);
    number("quality",s.quality);
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

    String json;
    json.reserve(420);
    json = "{\"ready\":";
    json += snap.ready ? "true" : "false";
    json += ",\"line\":\"";
    if (snap.ready) {
        for (uint8_t i = 0; i < 100; ++i) json += scopeChar(snap.bins[i]);
    }
    json += "\",\"samples\":";
    json += String(snap.samples);
    json += ",\"activeMs\":";
    json += String(snap.activeMs);
    json += ",\"coverage\":";
    json += String(snap.samples >= 1000 ? 100 : (snap.samples * 100UL) / 1000UL);
    json += ",\"phaseBin\":";
    json += String(snap.phaseBin);
    json += ",\"phaseQuality\":";
    json += String(snap.phaseQuality);
    json += ",\"phaseLocked\":";
    json += snap.phaseLocked ? "true" : "false";
    json += ",\"lastSymbol\":\"";
    if (snap.lastMinuteMarker) json += "MIN";
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
    json += snap.secondLocked ? "true" : "false";
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
        if (p.bit == 0) json += "0";
        else if (p.bit == 1) json += "1";
        else json += "?";
        json += "\",\"valid\":"; json += p.valid ? "true" : "false";
        json += ",\"glitch\":"; json += glitch ? "true" : "false";
        json += ",\"secondTimingOk\":"; json += p.secondTimingOk ? "true" : "false";
        json += ",\"timingLabel\":\"";
        if (p.minuteGap) json += "MIN";
        else if (p.secondTimingOk) json += "1s OK";
        else json += "fuori";
        json += "\",\"framePos\":"; json += String(p.framePos);
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
    json += selectedSignalMode == SignalMode::DCF77 ? "DCF77 77.5 kHz" : "RAW 60 kHz";
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
    if (!server.hasArg("mode")) {
        server.send(400,"application/json","{\"ok\":false,\"message\":\"Parametro mode mancante\"}");
        return;
    }
    const String mode = server.arg("mode");
    if (mode == "dcf77") selectedSignalMode = SignalMode::DCF77;
    else if (mode == "raw60") selectedSignalMode = SignalMode::RAW_60KHZ;
    else {
        server.send(400,"application/json","{\"ok\":false,\"message\":\"Decoder non valido\"}");
        return;
    }
    resetReceiverDiagnostics();
    server.send(200,"application/json",
        selectedSignalMode == SignalMode::DCF77
          ? "{\"ok\":true,\"message\":\"Decoder DCF77 77,5 kHz selezionato\"}"
          : "{\"ok\":true,\"message\":\"Analizzatore RAW 60 kHz selezionato\"}");
}

void setReceiverControl() {
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
    selectedSignalMode = SignalMode::DCF77;
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
    server.onNotFound([](){server.send(404,"text/plain","Not found");});
    server.begin();
}

void portalPoll(const DCF77Decoder &decoder, const ReceiverControl &receiver) {
    (void)receiver;
    currentDecoder=&decoder;
    pollScope();
    const uint32_t now = millis();
    const uint32_t total = decoder.stats().totalPulses;
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
        rateSamplePulses = total;
    } else if (now - rateSampleMs >= 1000) {
        const uint32_t elapsed = now - rateSampleMs;
        pulseEventsPerSecond = (total - rateSamplePulses) * 1000.0f / elapsed;
        rateSampleMs = now;
        rateSamplePulses = total;
    }
    server.handleClient();
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
void portalBegin() {}
void portalPoll(const DCF77Decoder &, const ReceiverControl &) {}
bool portalTakeReceiverResetRequest() { return false; }
bool portalDcfActiveLow() { return DCF77_ACTIVE_LOW; }
SignalMode portalSignalMode() { return SignalMode::DCF77; }
const char *portalAddress() { return ""; }
#endif
