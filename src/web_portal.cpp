#include "web_portal.h"
#include "config.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

namespace {
ESP8266WebServer server(80);
RawSignalStats currentStats;

constexpr uint8_t TRACE_SIZE = 24;
RawPulseSample traceBuf[TRACE_SIZE];
uint8_t traceHead = 0;
uint8_t traceCount = 0;

const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="it"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DCF77 RAW HW-364A</title>
<style>
:root{color-scheme:dark}*{box-sizing:border-box}body{font:15px system-ui;background:#10202d;color:#eef6fb;max-width:1180px;margin:auto;padding:18px}
h1{font-size:27px;margin:0 0 6px}.sub{color:#8fc7e8;margin-bottom:16px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}
.card{background:#193346;padding:16px;border-radius:12px;margin:14px 0}.wide{grid-column:1/-1}
.big{font-size:38px;font-weight:750}.ok{color:#62f0aa}.bad{color:#ff7f7f}.warn{color:#ffd479}.muted{color:#8ba8b7}
.pill{display:inline-flex;padding:6px 11px;border-radius:999px;background:#102a3a;font-weight:700}
dl{display:grid;grid-template-columns:1fr auto;gap:8px 16px;margin:0}dt{color:#a9ccdc}dd{margin:0;text-align:right}
table{width:100%;border-collapse:collapse}td,th{padding:7px;border-bottom:1px solid #355268;text-align:right}td:first-child,th:first-child{text-align:left}
.level{height:34px;border-radius:8px;background:#102a3a;overflow:hidden;margin-top:10px;position:relative}
.level>div{height:100%;width:0%;background:#62f0aa;transition:width .15s}.diaggrid{display:grid;grid-template-columns:repeat(4,1fr);gap:9px}
.diag{background:#102a3a;border-radius:8px;padding:10px}.diag small{display:block;color:#8ba8b7}.diag b{font-size:17px}
code{background:#102a3a;padding:2px 5px;border-radius:4px}
@media(max-width:760px){.grid{grid-template-columns:1fr}.diaggrid{grid-template-columns:1fr 1fr}}
</style>

<h1>DCF77 · RAW Receiver</h1>
<div class=sub>HW-364A + DCF-3850N-800 / SP6007 · prima acquisizione grezza, nessun decoder attivo</div>

<div class=grid>
  <div class=card>
    <div class=muted>DATA T · D7 / GPIO13</div>
    <div id=rawLevel class=big>--</div>
    <div class=level><div id=levelbar></div></div>
    <div class=muted style="margin-top:10px">P1 / PON: <b class=ok>LOW fisso · ricevitore ON</b></div>
  </div>
  <div class=card><dl id=summary></dl></div>

  <div class="card wide">
    <h2>Diagnostica RAW</h2>
    <div id=health class="pill warn">ATTESA SEGNALE</div>
    <div id=diag class=diaggrid style="margin-top:12px"></div>
  </div>

  <div class="card wide">
    <h2>Impulsi grezzi recenti</h2>
    <div class=muted>Per ora interpretiamo solo la forma elettrica. ~100 ms ≈ 0, ~200 ms ≈ 1. Il decoder DCF77 verrà riattivato dopo che il segnale RAW è stabile.</div>
    <table style="margin-top:10px"><thead><tr><th>Età</th><th>Larghezza</th><th>Periodo</th><th>Guess</th><th>Stato</th></tr></thead><tbody id=rows></tbody></table>
  </div>

  <div class=card>
    <h2>Cablaggio richiesto</h2>
    <dl>
      <dt>G</dt><dd>GND</dd>
      <dt>V</dt><dd>3.3 V max</dd>
      <dt>T</dt><dd>D7 / GPIO13</dd>
      <dt>P1</dt><dd>D1 / GPIO5 · LOW</dd>
    </dl>
  </div>
  <div class=card>
    <h2>Cosa dobbiamo vedere</h2>
    <div class=muted>
      DATA normalmente LOW, poi impulsi HIGH circa una volta al secondo.<br><br>
      <code>~100 ms</code> bit 0<br>
      <code>~200 ms</code> bit 1<br>
      <code>~2 s</code> tra due inizi = marker minuto
    </div>
  </div>
</div>

<script>
const el=id=>document.getElementById(id);
async function update(){
  try{
    const d=await (await fetch('/api/raw',{cache:'no-store'})).json();
    el('rawLevel').textContent=d.dataLevel?'HIGH':'LOW';
    el('rawLevel').className='big '+(d.dataLevel?'ok':'muted');
    el('levelbar').style.width=d.dataLevel?'100%':'0%';

    const age=d.lastEdgeAgeMs===4294967295?'mai':(d.lastEdgeAgeMs/1000).toFixed(1)+' s';
    const vals=[
      ['Edge totali',d.totalEdges],['Edge / secondo',d.edgesPerSecond],
      ['Impulsi',d.totalPulses],['Validi',d.validPulses],
      ['Ultimo edge',age],['P1',d.ponLow?'LOW / ON':'NON LOW']
    ];
    el('summary').innerHTML=vals.map(x=>'<dt>'+x[0]+'</dt><dd>'+x[1]+'</dd>').join('');

    let healthClass='warn',health='ATTESA SEGNALE';
    if(d.totalEdges>0 && d.lastEdgeAgeMs<3000){healthClass='ok';health='SEGNALE ATTIVO'}
    if(d.totalEdges>0 && d.lastEdgeAgeMs>=5000){healthClass='bad';health='SEGNALE FERMO'}
    el('health').className='pill '+healthClass;el('health').textContent=health;

    const ratio=d.totalPulses?Math.round(d.validPulses*100/d.totalPulses):0;
    const diag=[
      ['DATA',d.dataLevel?'HIGH':'LOW'],
      ['Pulse', (d.lastPulseUs/1000).toFixed(1)+' ms'],
      ['Period',(d.lastPeriodUs/1000).toFixed(1)+' ms'],
      ['Guess',d.lastBitGuess<0?'?':d.lastBitGuess],
      ['Valid ratio',ratio+'%'],
      ['Minute gap',d.minuteGaps],
      ['Edges/s',d.edgesPerSecond],
      ['P1/PON',d.ponLow?'LOW':'ERR']
    ];
    el('diag').innerHTML=diag.map(x=>'<div class=diag><small>'+x[0]+'</small><b>'+x[1]+'</b></div>').join('');

    const p=await (await fetch('/api/pulses',{cache:'no-store'})).json();
    el('rows').innerHTML=p.pulses.map(x=>'<tr class="'+(x.valid?'ok':'bad')+'"><td>'+(x.ageMs/1000).toFixed(1)+' s</td><td>'+(x.widthUs/1000).toFixed(1)+' ms</td><td>'+(x.periodUs/1000).toFixed(1)+' ms</td><td>'+(x.bitGuess<0?'?':x.bitGuess)+'</td><td>'+(x.valid?'OK':'RAW')+'</td></tr>').join('');
  }catch(e){}
}
setInterval(update,500);update();
</script></html>)HTML";

void sendRaw(){
    String j; j.reserve(700);
    j="{\"totalEdges\":"+String(currentStats.totalEdges);
    j+=",\"totalPulses\":"+String(currentStats.totalPulses);
    j+=",\"validPulses\":"+String(currentStats.validPulses);
    j+=",\"invalidPulses\":"+String(currentStats.invalidPulses);
    j+=",\"minuteGaps\":"+String(currentStats.minuteGaps);
    j+=",\"lastPulseUs\":"+String(currentStats.lastPulseUs);
    j+=",\"lastPeriodUs\":"+String(currentStats.lastPeriodUs);
    j+=",\"lastEdgeAgeMs\":"+String(currentStats.lastEdgeAgeMs);
    j+=",\"edgesPerSecond\":"+String(currentStats.edgesPerSecond);
    j+=",\"lastBitGuess\":"+String(currentStats.lastBitGuess);
    j+=",\"dataLevel\":";j+=currentStats.dataLevel?"true":"false";
    j+=",\"ponLow\":";j+=currentStats.ponLow?"true":"false";
    j+="}";
    server.sendHeader("Cache-Control","no-store");
    server.send(200,"application/json",j);
}

void sendPulses(){
    String j="{\"pulses\":[";
    const uint32_t now=millis();
    for(uint8_t n=0;n<traceCount;++n){
        int i=(int)traceHead-1-n;while(i<0)i+=TRACE_SIZE;
        const RawPulseSample &p=traceBuf[i];
        if(n)j+=',';
        j+="{\"ageMs\":"+String(now-p.ageMs);
        j+=",\"widthUs\":"+String(p.widthUs);
        j+=",\"periodUs\":"+String(p.periodUs);
        j+=",\"bitGuess\":"+String(p.bitGuess);
        j+=",\"valid\":";j+=p.valid?"true":"false";j+="}";
    }
    j+="]}";
    server.sendHeader("Cache-Control","no-store");
    server.send(200,"application/json",j);
}
}

void portalBegin(){
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    char ssid[32];snprintf(ssid,sizeof(ssid),"DCF77-RAW-%06X",ESP.getChipId());
    WiFi.softAP(ssid);
    WiFi.begin();

    server.on("/",HTTP_GET,[](){server.send_P(200,"text/html; charset=utf-8",PAGE);});
    server.on("/api/raw",HTTP_GET,sendRaw);
    server.on("/api/pulses",HTTP_GET,sendPulses);
    server.begin();
}

void portalPoll(){server.handleClient();}
void portalReportRaw(const RawSignalStats &stats){currentStats=stats;}

void portalPushPulse(const RawPulseSample &sample){
    RawPulseSample p=sample;
    p.ageMs=millis();
    traceBuf[traceHead]=p;
    traceHead=(traceHead+1U)%TRACE_SIZE;
    if(traceCount<TRACE_SIZE)++traceCount;
}

const char *portalAddress(){return "192.168.4.1";}
