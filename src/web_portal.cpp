#include "web_portal.h"
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

namespace {
ESP8266WebServer server(80);
const DCF77Decoder *currentDecoder = nullptr;
uint32_t observedPulses = 0, lastPulseMs = 0;
const char page[] PROGMEM = R"HTML(<!doctype html><html lang="it"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DCF77 HW-364A</title><style>body{font:16px system-ui;background:#10202d;color:#eaf3fa;max-width:800px;margin:auto;padding:20px}h1{font-size:24px}#clock{font-size:40px}dl{display:grid;grid-template-columns:1fr 1fr;gap:12px;background:#193346;padding:20px;border-radius:12px}dd{margin:0;text-align:right}#status{color:#ffd580}pre{white-space:pre-wrap;overflow-wrap:anywhere}</style>
<h1>DCF77 · HW-364A</h1><div id="clock">--:--:--</div><p id="date">Attesa ricezione</p><p id="status">Connessione…</p><dl id="metrics"></dl><h2>Ultimo frame</h2><pre id="frame">—</pre>
<script>
const labels={quality:'Qualità temporale (%)',frameBitCount:'Posizione frame',lastBit:'Ultimo bit',pulseMs:'Impulso (ms)',periodMs:'Periodo (ms)',jitterMs:'Jitter (ms)',rmsMs:'Jitter RMS (ms)',validPulses:'Impulsi validi',invalidPulses:'Impulsi invalidi',validFrames:'Frame validi',invalidFrames:'Frame invalidi',parityErrors:'Errori parità',timingErrors:'Errori temporali',glitches:'Glitch',frameAgeSeconds:'Età ultimo frame (s)',ppsUs:'Offset PPS (µs)',freeHeap:'RAM libera (byte)'};
async function update(){if(document.hidden)return;try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw Error();const d=await r.json();document.getElementById('clock').textContent=d.time||'--:--:--';document.getElementById('date').textContent=d.date||'Attesa frame valido';document.getElementById('status').textContent=d.signalRecent?(d.clockAvailable?'Segnale presente · orologio disponibile':'Segnale presente · acquisizione'):'Segnale assente o non ancora ricevuto';const list=document.getElementById('metrics');list.replaceChildren();for(const [k,l]of Object.entries(labels)){const a=document.createElement('dt'),b=document.createElement('dd');a.textContent=l;b.textContent=d[k]??'—';list.append(a,b)}document.getElementById('frame').textContent=d.frame||'Nessun frame ricevuto';}catch(e){document.getElementById('status').textContent='Connessione al dispositivo persa';}}
setInterval(update,2000);document.addEventListener('visibilitychange',update);update();
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
}
void portalBegin() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    char ssid[32]; snprintf(ssid,sizeof(ssid),"DCF77-HW364A-%06X",ESP.getChipId());
    // Local read-only AP; change password before sharing the device.
    if (!WiFi.softAP(ssid,"dcf77oled")) Serial.println("WiFi AP startup failed");
    Serial.printf("WiFi: %s; password: dcf77oled; http://192.168.4.1\n",ssid);
    server.on("/",HTTP_GET,[](){server.send_P(200,"text/html; charset=utf-8",page);});
    server.on("/api/status",HTTP_GET,status);
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
