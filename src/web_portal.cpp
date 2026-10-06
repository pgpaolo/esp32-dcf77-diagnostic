#include "web_portal.h"
#include "config.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

namespace {
ESP8266WebServer server(80);
DCF77Decoder *decoderPtr = nullptr;
bool resetRequested = false;
DecodeMode configuredMode = DecodeMode::ACCUMULATE;
OledViewMode configuredDisplayMode = OledViewMode::AUTO;

constexpr uint8_t EEPROM_MAGIC = 0xD7;
constexpr uint8_t EEPROM_ADDR_MAGIC = 0;
constexpr uint8_t EEPROM_ADDR_MODE = 1;
constexpr uint8_t EEPROM_ADDR_DISPLAY = 2;

const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="it"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DCF77 HW-364A</title>
<style>
:root{color-scheme:dark}*{box-sizing:border-box}body{font:15px system-ui;background:#10202d;color:#eef6fb;max-width:1180px;margin:auto;padding:18px}
h1{font-size:26px;margin:0 0 8px}.sub{color:#8fc7e8;margin-bottom:16px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}
.card{background:#193346;padding:16px;border-radius:12px;margin:14px 0}.wide{grid-column:1/-1}
.clock{font-size:42px;font-weight:750;letter-spacing:1px}.date{color:#b8d8e8;font-size:18px}
dl{display:grid;grid-template-columns:1fr auto;gap:8px 16px;margin:0}dt{color:#a9ccdc}dd{margin:0;text-align:right}
.ok{color:#62f0aa}.bad{color:#ff7f7f}.warn{color:#ffd479}.muted{color:#8ba8b7}
button,input,select{padding:9px 11px;border-radius:8px;border:0;margin:3px;background:#eaf3f8;color:#132632}
button{cursor:pointer}.modebar{display:flex;align-items:center;gap:8px;flex-wrap:wrap}
.modepill{padding:6px 10px;border-radius:999px;background:#102a3a;color:#8fc7e8;font-weight:700}
table{width:100%;border-collapse:collapse}td,th{padding:7px;border-bottom:1px solid #355268;text-align:right}td:first-child,th:first-child{text-align:left}
.framewrap{overflow-x:auto}.frame{display:grid;grid-template-columns:repeat(59,minmax(26px,1fr));gap:3px;min-width:1040px;margin-top:10px}
.bit{height:46px;border-radius:5px;background:#102a3a;display:flex;flex-direction:column;align-items:center;justify-content:center;border:1px solid #2f5368}
.bit b{font-size:15px}.bit small{font-size:9px;color:#8faebd}.b0{background:#163d50}.b1{background:#2f5d43}.bq{background:#5a4330}.future{opacity:.35}
.f-service{border-bottom:3px solid #6c8da0}.f-zone{border-bottom:3px solid #d6a84c}.f-minute{border-bottom:3px solid #5ac88b}.f-hour{border-bottom:3px solid #67a7e8}.f-date{border-bottom:3px solid #d17bb5}
.legend{display:flex;gap:14px;flex-wrap:wrap;color:#9fc7d8;font-size:13px;margin-top:10px}.dot{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:4px}
.progress{height:10px;background:#102a3a;border-radius:99px;overflow:hidden;margin-top:8px}.progress>div{height:100%;background:#62f0aa}
.health{display:inline-flex;align-items:center;gap:7px;padding:6px 10px;border-radius:999px;background:#102a3a;font-weight:700}
.health.good{color:#62f0aa}.health.mid{color:#ffd479}.health.bad{color:#ff7f7f}
.diaggrid{display:grid;grid-template-columns:repeat(3,1fr);gap:9px 18px}.diagitem{background:#102a3a;border-radius:8px;padding:10px}.diagitem small{display:block;color:#8ba8b7}.diagitem b{font-size:17px}
@media(max-width:760px){.grid{grid-template-columns:1fr}.clock{font-size:34px}.diaggrid{grid-template-columns:1fr 1fr}}
</style>
<h1>DCF77 · HW-364A</h1>
<div class=sub>Ricevitore 77,5 kHz · decodifica diretta o accumulo stile radio-controlled clock</div>

<div class=card>
  <div class=modebar>
    <b>Modalità decoder</b>
    <span id=modePill class=modepill>ACCUMULO</span>
    <select id=modeSel><option value=accumulate>Accumulo / radio clock</option><option value=direct>Diretta / strict</option></select>
    <button onclick="setMode()">Applica</button>
    <span id=modeMsg class=muted></span>
  </div>
</div>

<div class=card>
  <div class=modebar>
    <b>Visualizzazione OLED</b>
    <span id=displayPill class=modepill>AUTO</span>
    <select id=displaySel>
      <option value=auto>Auto rotazione</option>
      <option value=clock>Solo ora/data</option>
      <option value=signal>Segnale DCF77</option>
      <option value=decoder>Decoder / accumulo</option>
      <option value=diagnostics>Diagnostica</option>
    </select>
    <button onclick="setDisplay()">Salva</button>
    <span id=displayMsg class=muted></span>
  </div>
</div>

<div class=grid>
  <div class=card><div id=clock class=clock>--:--:--</div><div id=date class=date>Attesa frame valido</div></div>
  <div class=card><dl id=stats></dl></div>
  <div class="card wide">
    <h2>Accumulo e decodifica bit</h2>
    <div id=accumText class=muted>Attesa marker minuto…</div>
    <div class=progress><div id=frameProgress style="width:0%"></div></div>
    <div class=framewrap><div id=frame class=frame></div></div>
    <div class=legend>
      <span><i class="dot" style="background:#163d50"></i>bit 0</span>
      <span><i class="dot" style="background:#2f5d43"></i>bit 1</span>
      <span><i class="dot" style="background:#5a4330"></i>incerto</span>
      <span>0-15 servizio</span><span>16-20 zona/controllo</span><span>21-28 minuti</span><span>29-35 ore</span><span>36-58 data</span>
    </div>
  </div>
  <div class="card wide">
    <h2>Diagnostica avanzata</h2>
    <div style="display:flex;gap:12px;align-items:center;flex-wrap:wrap;margin-bottom:12px">
      <span id=health class="health mid">VALUTAZIONE…</span>
      <span id=recentMix class=muted></span>
    </div>
    <div id=diag class=diaggrid></div>
  </div>
  <div class="card wide"><h2>Impulsi recenti</h2><table><thead><tr><th>Età</th><th>Impulso</th><th>Periodo</th><th>Bit</th><th>Conf.</th><th>Stato</th></tr></thead><tbody id=rows></tbody></table></div>
  <div class=card><h2>Wi-Fi</h2><button onclick="scan()">Scansiona</button><select id=ssid></select><input id=pass type=password placeholder="Password"><button onclick="connectWifi()">Connetti</button><div id=wifi></div></div>
  <div class=card><h2>Controlli</h2><button onclick="resetDecoder()">Reset decoder</button><div class=muted>Il cambio modalità azzera l'acquisizione per evitare di mescolare frame costruiti con criteri diversi.</div></div>
</div>

<script>
const el=id=>document.getElementById(id);
function fieldClass(i){if(i<=15)return'f-service';if(i<=20)return'f-zone';if(i<=28)return'f-minute';if(i<=35)return'f-hour';return'f-date'}
function healthLabel(d){
  if(!d.minuteSynced || d.quality<45 || d.validRatio<60)return['bad','CRITICO'];
  if(!d.clockLocked || d.quality<75 || d.validRatio<80)return['mid','ATTENZIONE'];
  return['good','BUONO'];
}
function renderFrame(f){
  const bits=f.bits||[], conf=f.confidence||[], count=f.count||0;
  el('frame').innerHTML='';
  for(let i=0;i<59;i++){
    const b=bits[i]===undefined?null:bits[i],c=conf[i]||0,d=document.createElement('div');
    d.className='bit '+fieldClass(i)+' '+(i>=count?'future':(b===0?'b0':b===1?'b1':'bq'));
    d.title='Secondo '+i+' · confidenza '+c+'%';
    d.innerHTML='<b>'+(i>=count?'·':(b===0?'0':b===1?'1':'?'))+'</b><small>'+i+(i<count?' · '+c+'%':'')+'</small>';
    el('frame').appendChild(d);
  }
  el('frameProgress').style.width=Math.min(100,count*100/59)+'%';
}
async function update(){
  try{
    const d=await (await fetch('/api/status',{cache:'no-store'})).json();
    el('clock').textContent=d.clockAvailable?d.time:'--:--:--';el('date').textContent=d.clockAvailable?d.date:'Attesa frame valido';
    el('modePill').textContent=d.modeLabel;el('modeSel').value=d.mode==='accumulate'?'accumulate':'direct';
    el('displayPill').textContent=d.displayLabel;el('displaySel').value=d.displayMode;
    const vals=[['Stato minuto',d.minuteSynced?'SYNC':'SEARCH'],['Qualità',d.quality+'%'],['Posizione frame',d.frameBitCount+' / 59'],
      ['Impulso',d.pulseMs.toFixed(1)+' ms'],['Periodo',d.periodMs.toFixed(1)+' ms'],['Marker minuto',d.minuteMarkers],
      ['Frame validi',d.validFrames],['Frame invalidi',d.invalidFrames],['Parità KO',d.parityErrors],['Timing KO',d.timingErrors]];
    el('stats').innerHTML=vals.map(x=>'<dt>'+x[0]+'</dt><dd>'+x[1]+'</dd>').join('');
    if(d.mode==='accumulate'){
      el('accumText').innerHTML='Candidati coerenti: <b>'+d.candidateMinutes+'</b> · confidenza campi: <b>'+d.fieldConfidence+'%</b> · bit incerti: <b>'+d.uncertainBits+'</b> · recuperati: <b>'+d.recoveredBits+'</b>';
    }else{
      el('accumText').innerHTML='Modalità diretta: il frame viene accettato solo se tutti i 59 bit sono presenti e P1/P2/P3 risultano valide.';
    }
    const h=healthLabel(d);el('health').className='health '+h[0];el('health').textContent='STATO '+h[1];
    const diagVals=[
      ['Uptime',d.uptime],['Heap libero',d.freeHeap+' B'],['Wi-Fi RSSI',d.rssi===null?'—':d.rssi+' dBm'],
      ['Jitter RMS',d.jitterRmsMs.toFixed(2)+' ms'],['Impulsi validi',d.validRatio+'%'],['Glitch',d.glitchCount],
      ['Ultimo frame valido',d.lastValidAgeSec===null?'mai':d.lastValidAgeSec+' s fa'],['Clock lock',d.clockLocked?'SI':'NO'],
      ['Parità','P1 '+(d.p1?'OK':'KO')+' · P2 '+(d.p2?'OK':'KO')+' · P3 '+(d.p3?'OK':'KO')]
    ];
    el('diag').innerHTML=diagVals.map(x=>'<div class=diagitem><small>'+x[0]+'</small><b>'+x[1]+'</b></div>').join('');
    const f=await (await fetch('/api/frame',{cache:'no-store'})).json();renderFrame(f.current);
    const p=await (await fetch('/api/pulses',{cache:'no-store'})).json();
    let z=0,o=0,q=0;p.pulses.forEach(x=>{if(x.bit===0)z++;else if(x.bit===1)o++;else q++});
    el('recentMix').textContent='Ultimi '+p.pulses.length+' eventi: 0='+z+' · 1='+o+' · ?='+q;
    el('rows').innerHTML=p.pulses.map(x=>'<tr class="'+(x.valid?'ok':'bad')+'"><td>'+(x.ageMs/1000).toFixed(1)+'s</td><td>'+x.widthMs.toFixed(1)+' ms</td><td>'+x.periodMs.toFixed(1)+' ms</td><td>'+x.bit+'</td><td>'+x.confidence+'%</td><td>'+(x.minuteGap?'MIN':(x.valid?'OK':'KO'))+'</td></tr>').join('');
  }catch(e){}
}
async function setMode(){
  const body=new URLSearchParams({mode:el('modeSel').value});
  const r=await fetch('/api/mode',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  const d=await r.json();el('modeMsg').textContent=d.message||'';setTimeout(update,300);
}
async function setDisplay(){
  const body=new URLSearchParams({display:el('displaySel').value});
  const r=await fetch('/api/display',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  const d=await r.json();el('displayMsg').textContent=d.message||'';setTimeout(update,300);
}
async function scan(){const d=await(await fetch('/api/networks')).json();el('ssid').innerHTML=d.networks.map(n=>'<option>'+n+'</option>').join('')}
async function connectWifi(){await fetch('/api/wifi/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({ssid:el('ssid').value,password:el('pass').value})})}
async function resetDecoder(){await fetch('/api/reset',{method:'POST'});setTimeout(update,300)}
setInterval(update,1200);update();
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

void saveMode(DecodeMode mode) {
    EEPROM.write(EEPROM_ADDR_MAGIC, EEPROM_MAGIC);
    EEPROM.write(EEPROM_ADDR_MODE, mode == DecodeMode::ACCUMULATE ? 1 : 0);
    EEPROM.commit();
}

const char *displayValue(OledViewMode mode) {
    switch(mode){
        case OledViewMode::CLOCK: return "clock";
        case OledViewMode::SIGNAL: return "signal";
        case OledViewMode::DECODER: return "decoder";
        case OledViewMode::DIAGNOSTICS: return "diagnostics";
        default: return "auto";
    }
}

const char *displayLabel(OledViewMode mode) {
    switch(mode){
        case OledViewMode::CLOCK: return "ORA";
        case OledViewMode::SIGNAL: return "SEGNALE";
        case OledViewMode::DECODER: return "DECODER";
        case OledViewMode::DIAGNOSTICS: return "DIAGNOSTICA";
        default: return "AUTO";
    }
}

void saveDisplayMode(OledViewMode mode) {
    EEPROM.write(EEPROM_ADDR_MAGIC, EEPROM_MAGIC);
    EEPROM.write(EEPROM_ADDR_DISPLAY, static_cast<uint8_t>(mode));
    EEPROM.commit();
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
    const uint32_t totalPulseQuality = s.validPulses + s.invalidPulses;
    const uint8_t validRatio = totalPulseQuality
        ? static_cast<uint8_t>((s.validPulses * 100UL) / totalPulseQuality) : 0;
    const bool staConnected = WiFi.status() == WL_CONNECTED;
    const int rssi = staConnected ? WiFi.RSSI() : 0;
    const uint32_t upSec = millis()/1000UL;
    char uptime[24];
    snprintf(uptime,sizeof(uptime),"%lud %02lu:%02lu:%02lu",
             (unsigned long)(upSec/86400UL),
             (unsigned long)((upSec/3600UL)%24UL),
             (unsigned long)((upSec/60UL)%60UL),
             (unsigned long)(upSec%60UL));

    String j; j.reserve(1200);
    j="{\"clockAvailable\":";j+=clock?"true":"false";
    j+=",\"time\":\""+String(t)+"\",\"date\":\""+String(d)+"\"";
    j+=",\"mode\":\"";j+=decoderPtr->decodeMode()==DecodeMode::ACCUMULATE?"accumulate":"direct";j+="\"";
    j+=",\"modeLabel\":\"";j+=decoderPtr->decodeModeLabel();j+="\"";
    j+=",\"displayMode\":\"";j+=displayValue(configuredDisplayMode);j+="\"";
    j+=",\"displayLabel\":\"";j+=displayLabel(configuredDisplayMode);j+="\"";
    j+=",\"minuteSynced\":";j+=s.minuteSynced?"true":"false";
    j+=",\"clockLocked\":";j+=s.clockLocked?"true":"false";
    j+=",\"quality\":"+String(s.quality);
    j+=",\"frameBitCount\":"+String(s.frameBitCount);
    j+=",\"pulseMs\":"+String(s.lastPulseWidthUs/1000.0,3);
    j+=",\"periodMs\":"+String(s.lastPeriodUs/1000.0,3);
    j+=",\"minuteMarkers\":"+String(s.minuteMarkers);
    j+=",\"validFrames\":"+String(s.validFrames);
    j+=",\"invalidFrames\":"+String(s.invalidFrames);
    j+=",\"parityErrors\":"+String(s.parityErrors);
    j+=",\"timingErrors\":"+String(s.timingErrors);
    j+=",\"candidateMinutes\":"+String(s.candidateMinutes);
    j+=",\"fieldConfidence\":"+String(s.fieldConfidence);
    j+=",\"uncertainBits\":"+String(s.uncertainBits);
    j+=",\"recoveredBits\":"+String(s.recoveredBits);
    j+=",\"uptime\":\""+String(uptime)+"\"";
    j+=",\"freeHeap\":"+String(ESP.getFreeHeap());
    j+=",\"rssi\":"; if(staConnected) j+=String(rssi); else j+="null";
    j+=",\"jitterRmsMs\":"+String(s.jitterRmsUs/1000.0f,3);
    j+=",\"validRatio\":"+String(validRatio);
    j+=",\"glitchCount\":"+String(s.glitchCount);
    j+=",\"lastValidAgeSec\":";
    if(s.lastValidFrameMs) j+=String((millis()-s.lastValidFrameMs)/1000UL); else j+="null";
    j+=",\"p1\":";j+=s.parityMinute?"true":"false";
    j+=",\"p2\":";j+=s.parityHour?"true":"false";
    j+=",\"p3\":";j+=s.parityDate?"true":"false";
    j+="}";
    server.sendHeader("Cache-Control","no-store");server.send(200,"application/json",j);
}

void appendFrameJson(String &j,const char *name,const int8_t *bits,const uint8_t *conf,uint8_t count){
    j+="\"";j+=name;j+="\":{\"count\":"+String(count)+",\"bits\":[";
    for(uint8_t i=0;i<59;++i){if(i)j+=',';j+=i<count?String(bits[i]):String("-1");}
    j+="],\"confidence\":[";
    for(uint8_t i=0;i<59;++i){if(i)j+=',';j+=i<count?String(conf[i]):String("0");}
    j+="]}";
}

void frameStatus(){
    if(!decoderPtr){server.send(503,"application/json","{}");return;}
    String j; j.reserve(1000); j="{";
    appendFrameJson(j,"current",decoderPtr->currentFrameBits(),decoderPtr->currentFrameConfidence(),decoderPtr->currentFrameCount());
    j+=",";
    appendFrameJson(j,"last",decoderPtr->lastFrameBits(),decoderPtr->lastFrameConfidence(),decoderPtr->lastFrameCount());
    j+="}";
    server.sendHeader("Cache-Control","no-store");server.send(200,"application/json",j);
}

void pulses() {
    if (!decoderPtr) { server.send(503,"application/json","{}"); return; }
    String j; j.reserve(3000); j="{\"pulses\":[";
    const uint32_t now=millis();
    for(uint8_t i=0;i<decoderPtr->recentPulseCount();++i){
        PulseTrace p;if(!decoderPtr->recentPulse(i,p))continue;if(i)j+=',';
        j+="{\"ageMs\":"+String(now-p.capturedMs);
        j+=",\"widthMs\":"+String(p.widthUs/1000.0,3);
        j+=",\"periodMs\":"+String(p.periodUs/1000.0,3);
        j+=",\"bit\":"+String(p.bit);
        j+=",\"confidence\":"+String(p.confidence);
        j+=",\"valid\":";j+=p.valid?"true":"false";
        j+=",\"minuteGap\":";j+=p.minuteGap?"true":"false";j+="}";
    }
    j+="]}";server.sendHeader("Cache-Control","no-store");server.send(200,"application/json",j);
}

void setMode() {
    if(!decoderPtr || !server.hasArg("mode")){server.send(400,"application/json","{\"ok\":false}");return;}
    const String m=server.arg("mode");
    DecodeMode mode;
    if(m=="accumulate") mode=DecodeMode::ACCUMULATE;
    else if(m=="direct") mode=DecodeMode::DIRECT;
    else {server.send(400,"application/json","{\"ok\":false,\"message\":\"Modalità non valida\"}");return;}

    configuredMode=mode;
    decoderPtr->setDecodeMode(mode);
    saveMode(mode);
    server.send(200,"application/json",
        mode==DecodeMode::ACCUMULATE
        ? "{\"ok\":true,\"message\":\"Modalità ACCUMULO attiva\"}"
        : "{\"ok\":true,\"message\":\"Modalità DIRETTA attiva\"}");
}

void setDisplay() {
    if(!server.hasArg("display")){
        server.send(400,"application/json","{\"ok\":false}");
        return;
    }

    const String v=server.arg("display");
    OledViewMode mode=OledViewMode::AUTO;
    if(v=="clock") mode=OledViewMode::CLOCK;
    else if(v=="signal") mode=OledViewMode::SIGNAL;
    else if(v=="decoder") mode=OledViewMode::DECODER;
    else if(v=="diagnostics") mode=OledViewMode::DIAGNOSTICS;
    else if(v!="auto"){
        server.send(400,"application/json","{\"ok\":false,\"message\":\"Visualizzazione non valida\"}");
        return;
    }

    configuredDisplayMode=mode;
    saveDisplayMode(mode);
    String j="{\"ok\":true,\"message\":\"OLED: ";
    j+=displayLabel(mode);
    j+="\"}";
    server.send(200,"application/json",j);
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
    WiFi.persistent(true);
    if(pass.length()) WiFi.begin(ssid.c_str(),pass.c_str()); else WiFi.begin(ssid.c_str());
    WiFi.persistent(false);
    server.send(202,"application/json","{\"ok\":true}");
}
}

void portalBegin() {
    EEPROM.begin(8);
    if(EEPROM.read(EEPROM_ADDR_MAGIC)==EEPROM_MAGIC){
        configuredMode=EEPROM.read(EEPROM_ADDR_MODE)==0?DecodeMode::DIRECT:DecodeMode::ACCUMULATE;
        const uint8_t dm=EEPROM.read(EEPROM_ADDR_DISPLAY);
        if(dm<=static_cast<uint8_t>(OledViewMode::DIAGNOSTICS))
            configuredDisplayMode=static_cast<OledViewMode>(dm);
    }

    WiFi.persistent(false);WiFi.mode(WIFI_AP_STA);
    char ssid[32];snprintf(ssid,sizeof(ssid),"DCF77-HW364A-%06X",ESP.getChipId());
    WiFi.softAP(ssid);WiFi.begin();

    server.on("/",HTTP_GET,[](){server.send_P(200,"text/html; charset=utf-8",PAGE);});
    server.on("/api/status",HTTP_GET,status);
    server.on("/api/frame",HTTP_GET,frameStatus);
    server.on("/api/pulses",HTTP_GET,pulses);
    server.on("/api/mode",HTTP_POST,setMode);
    server.on("/api/display",HTTP_POST,setDisplay);
    server.on("/api/wifi",HTTP_GET,wifiStatus);
    server.on("/api/networks",HTTP_GET,networks);
    server.on("/api/wifi/connect",HTTP_POST,connectWifi);
    server.on("/api/reset",HTTP_POST,[](){resetRequested=true;server.send(200,"application/json","{\"ok\":true}");});
    server.begin();
}

void portalPoll(DCF77Decoder &decoder) {
    decoderPtr=&decoder;
    if(decoder.decodeMode()!=configuredMode) decoder.setDecodeMode(configuredMode);
    server.handleClient();
}

bool portalTakeResetRequest(){bool r=resetRequested;resetRequested=false;return r;}
const char *portalAddress(){return "192.168.4.1";}
OledViewMode portalDisplayMode(){return configuredDisplayMode;}
const char *portalDisplayModeLabel(){return displayLabel(configuredDisplayMode);}
