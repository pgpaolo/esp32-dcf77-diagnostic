#include "web_portal.h"
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

namespace {
ESP8266WebServer server(80);
const DCF77Decoder *currentDecoder = nullptr;
uint32_t observedPulses = 0, lastPulseMs = 0;

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

<dl id="metrics"></dl>
<h2>Ultimo frame</h2><pre id="frame">—</pre>

<script>
const labels={quality:'Qualità temporale (%)',frameBitCount:'Posizione frame',lastBit:'Ultimo bit',pulseMs:'Impulso (ms)',periodMs:'Periodo (ms)',jitterMs:'Jitter (ms)',rmsMs:'Jitter RMS (ms)',validPulses:'Impulsi validi',invalidPulses:'Impulsi invalidi',validFrames:'Frame validi',invalidFrames:'Frame invalidi',parityErrors:'Errori parità',timingErrors:'Errori temporali',glitches:'Glitch',frameAgeSeconds:'Età ultimo frame (s)',ppsUs:'Offset PPS (µs)',freeHeap:'RAM libera (byte)'};

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
    for(const [k,l]of Object.entries(labels)){const a=document.createElement('dt'),b=document.createElement('dd');a.textContent=l;b.textContent=d[k]??'—';list.append(a,b)}
    document.getElementById('frame').textContent=d.frame||'Nessun frame ricevuto';
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
document.addEventListener('visibilitychange',()=>{update();updateWifi()});
update();updateWifi();
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
    number("quality",s.quality); number("frameBitCount",s.frameBitCount); number("lastBit",s.lastBit);
    number("pulseMs",s.lastPulseWidthUs/1000.0); number("periodMs",s.lastPeriodUs/1000.0);
    number("jitterMs",s.lastJitterUs/1000.0); number("rmsMs",s.jitterRmsUs/1000.0);
    number("validPulses",s.validPulses); number("invalidPulses",s.invalidPulses);
    number("validFrames",s.validFrames); number("invalidFrames",s.invalidFrames);
    number("parityErrors",s.parityErrors); number("timingErrors",s.timingErrors); number("glitches",s.glitchCount);
    number("freeHeap",ESP.getFreeHeap());
    json += ",\"frameAgeSeconds\":";
    json += s.validFrames ? String((millis()-s.lastValidFrameMs)/1000) : String("null");
    json += ",\"ppsUs\":"; json += s.lastPpsOffsetUs == INT32_MIN ? String("null") : String(s.lastPpsOffsetUs);
    json += ",\"frame\":\"";
    const int8_t *bits = currentDecoder->lastFrameBits();
    for (uint8_t i=0;i<currentDecoder->lastFrameCount();++i) json += bits[i]<0?'?':(bits[i]?'1':'0');
    json += "\"}";
    server.sendHeader("Cache-Control","no-store");
    server.send(200,"application/json",json);
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
    server.on("/api/wifi",HTTP_GET,wifiStatus);
    server.on("/api/networks",HTTP_GET,scanNetworks);
    server.on("/api/wifi/connect",HTTP_POST,connectWifi);
    server.onNotFound([](){server.send(404,"text/plain","Not found");});
    server.begin();
}

void portalPoll(const DCF77Decoder &decoder, const ReceiverControl &receiver) {
    (void)receiver;
    currentDecoder=&decoder;
    if (observedPulses != decoder.stats().totalPulses) {
        observedPulses = decoder.stats().totalPulses;
        lastPulseMs = millis();
    }
    server.handleClient();
}

const char *portalAddress() { return "192.168.4.1"; }
#else
void portalBegin() {}
void portalPoll(const DCF77Decoder &, const ReceiverControl &) {}
const char *portalAddress() { return ""; }
#endif
