#include "web_portal.h"
#include "config.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

namespace {
ESP8266WebServer server(80);
const DCF77Decoder *decoderPtr = nullptr;
bool resetRequested = false;

const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="it"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DCF77 HW-364A</title>
<style>
body{font:16px system-ui;background:#10202d;color:#eef6fb;max-width:900px;margin:auto;padding:20px}
.card{background:#193346;padding:18px;border-radius:12px;margin:14px 0}h1{font-size:24px}
dl{display:grid;grid-template-columns:1fr 1fr;gap:10px}dd{margin:0;text-align:right}
table{width:100%;border-collapse:collapse}td,th{padding:8px;border-bottom:1px solid #355268;text-align:right}
td:first-child,th:first-child{text-align:left}.ok{color:#62f0aa}.bad{color:#ff7f7f}
button,input,select{padding:10px;border-radius:8px;border:0;margin:4px}
</style>
<h1>DCF77 · HW-364A</h1>
<div class=card><div id=clock style="font-size:38px">--:--:--</div><div id=date>Attesa frame valido</div></div>
<div class=card><dl id=stats></dl></div>
<div class=card><h2>Impulsi recenti</h2><table><thead><tr><th>Età</th><th>ms</th><th>Periodo</th><th>Bit</th><th>Stato</th></tr></thead><tbody id=rows></tbody></table></div>
<div class=card><h2>Wi-Fi</h2><button onclick="scan()">Scansiona</button><select id=ssid></select><input id=pass type=password placeholder="Password"><button onclick="connectWifi()">Connetti</button><div id=wifi></div></div>
<div class=card><button onclick="resetDecoder()">Reset decoder</button></div>
<script>
async function update(){
  const d=await (await fetch('/api/status',{cache:'no-store'})).json();
  clock.textContent=d.clockAvailable?d.time:'--:--:--';date.textContent=d.clockAvailable?d.date:'Attesa frame valido';
  const vals=[['Stato minuto',d.minuteSynced?'SYNC':'SEARCH'],['Qualità',d.quality+'%'],['Posizione frame',d.frameBitCount],
  ['Impulso',d.pulseMs.toFixed(1)+' ms'],['Periodo',d.periodMs.toFixed(1)+' ms'],['Marker minuto',d.minuteMarkers],
  ['Frame validi',d.validFrames],['Frame invalidi',d.invalidFrames],['Parità KO',d.parityErrors],['Timing KO',d.timingErrors]];
  stats.innerHTML=vals.map(x=>'<dt>'+x[0]+'</dt><dd>'+x[1]+'</dd>').join('');
  const p=await (await fetch('/api/pulses',{cache:'no-store'})).json();
  rows.innerHTML=p.pulses.map(x=>'<tr class="'+(x.valid?'ok':'bad')+'"><td>'+(x.ageMs/1000).toFixed(1)+'s</td><td>'+x.widthMs.toFixed(1)+'</td><td>'+x.periodMs.toFixed(1)+'</td><td>'+x.bit+'</td><td>'+(x.minuteGap?'MIN':(x.valid?'OK':'KO'))+'</td></tr>').join('');
}
async function scan(){const d=await(await fetch('/api/networks')).json();ssid.innerHTML=d.networks.map(n=>'<option>'+n+'</option>').join('')}
async function connectWifi(){await fetch('/api/wifi/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({ssid:ssid.value,password:pass.value})})}
async function resetDecoder(){await fetch('/api/reset',{method:'POST'})}
setInterval(update,1500);update();
</script></html>)HTML";

String esc(const String &s) {
    String o;
    for (size_t i=0;i<s.length();++i) {
        char c=s[i];
        if(c=='\\'||c=='"') o+='\\';
        if(c>=32) o+=c;
    }
    return o;
}

void status() {
    if (!decoderPtr) { server.send(503,"application/json","{}"); return; }
    const auto &s=decoderPtr->stats();
    DCFDateTime dt;
    const bool clock=decoderPtr->getRunningClock(dt);
    char t[16]="",d[32]="";
    if(clock){
        snprintf(t,sizeof(t),"%02d:%02d:%02d",dt.hour,dt.minute,dt.second);
        snprintf(d,sizeof(d),"%02d/%02d/%04d %s",dt.day,dt.month,dt.year,dt.cest?"CEST":"CET");
    }
    String j="{\"clockAvailable\":";j+=clock?"true":"false";
    j+=",\"time\":\""+String(t)+"\",\"date\":\""+String(d)+"\"";
    j+=",\"minuteSynced\":";j+=s.minuteSynced?"true":"false";
    j+=",\"quality\":"+String(s.quality);
    j+=",\"frameBitCount\":"+String(s.frameBitCount);
    j+=",\"pulseMs\":"+String(s.lastPulseWidthUs/1000.0,3);
    j+=",\"periodMs\":"+String(s.lastPeriodUs/1000.0,3);
    j+=",\"minuteMarkers\":"+String(s.minuteMarkers);
    j+=",\"validFrames\":"+String(s.validFrames);
    j+=",\"invalidFrames\":"+String(s.invalidFrames);
    j+=",\"parityErrors\":"+String(s.parityErrors);
    j+=",\"timingErrors\":"+String(s.timingErrors)+"}";
    server.sendHeader("Cache-Control","no-store");server.send(200,"application/json",j);
}

void pulses() {
    if (!decoderPtr) { server.send(503,"application/json","{}"); return; }
    String j="{\"pulses\":[";
    const uint32_t now=millis();
    for(uint8_t i=0;i<decoderPtr->recentPulseCount();++i){
        PulseTrace p;if(!decoderPtr->recentPulse(i,p))continue;if(i)j+=',';
        j+="{\"ageMs\":"+String(now-p.capturedMs);
        j+=",\"widthMs\":"+String(p.widthUs/1000.0,3);
        j+=",\"periodMs\":"+String(p.periodUs/1000.0,3);
        j+=",\"bit\":"+String(p.bit);
        j+=",\"valid\":";j+=p.valid?"true":"false";
        j+=",\"minuteGap\":";j+=p.minuteGap?"true":"false";j+="}";
    }
    j+="]}";server.sendHeader("Cache-Control","no-store");server.send(200,"application/json",j);
}

void wifiStatus() {
    String j="{\"connected\":";j+=WiFi.status()==WL_CONNECTED?"true":"false";
    j+=",\"ssid\":\""+esc(WiFi.status()==WL_CONNECTED?WiFi.SSID():String(""))+"\"";
    j+=",\"ip\":\""+(WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String(""))+"\"}";
    server.send(200,"application/json",j);
}

void networks() {
    int n=WiFi.scanNetworks(false,true);String j="{\"networks\":[";
    bool first=true;
    for(int i=0;i<n;++i){if(!WiFi.SSID(i).length())continue;if(!first)j+=',';first=false;j+="\""+esc(WiFi.SSID(i))+"\"";}
    j+="]}";WiFi.scanDelete();server.send(200,"application/json",j);
}

void connectWifi() {
    const String ssid=server.arg("ssid"),pass=server.arg("password");
    if(!ssid.length()){server.send(400,"application/json","{\"ok\":false}");return;}
    WiFi.persistent(true);pass.length()?WiFi.begin(ssid.c_str(),pass.c_str()):WiFi.begin(ssid.c_str());WiFi.persistent(false);
    server.send(202,"application/json","{\"ok\":true}");
}
}

void portalBegin() {
    WiFi.persistent(false);WiFi.mode(WIFI_AP_STA);
    char ssid[32];snprintf(ssid,sizeof(ssid),"DCF77-HW364A-%06X",ESP.getChipId());
    WiFi.softAP(ssid);WiFi.begin();
    server.on("/",HTTP_GET,[](){server.send_P(200,"text/html; charset=utf-8",PAGE);});
    server.on("/api/status",HTTP_GET,status);
    server.on("/api/pulses",HTTP_GET,pulses);
    server.on("/api/wifi",HTTP_GET,wifiStatus);
    server.on("/api/networks",HTTP_GET,networks);
    server.on("/api/wifi/connect",HTTP_POST,connectWifi);
    server.on("/api/reset",HTTP_POST,[](){resetRequested=true;server.send(200,"application/json","{\"ok\":true}");});
    server.begin();
}

void portalPoll(const DCF77Decoder &decoder) { decoderPtr=&decoder;server.handleClient(); }
bool portalTakeResetRequest(){bool r=resetRequested;resetRequested=false;return r;}
const char *portalAddress(){return "192.168.4.1";}
