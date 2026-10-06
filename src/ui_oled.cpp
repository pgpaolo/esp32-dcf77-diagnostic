#include "ui.h"
#include "config.h"
#include "web_portal.h"
#include <Wire.h>

AnalyzerUI::AnalyzerUI() : _oled(128,64,&Wire,-1) {}

void AnalyzerUI::begin() {
    Wire.begin(PIN_OLED_SDA,PIN_OLED_SCL);
    _available = _oled.begin(SSD1306_SWITCHCAPVCC,OLED_ADDRESS,true,false);
    _lastPageMs = millis();
}

void AnalyzerUI::nextPage() {
    _page = (_page + 1U) % 4U;
    _lastPageMs = millis();
}

void AnalyzerUI::draw(const DCF77Decoder &decoder) {
    if (!_available || millis()-_lastDrawMs < OLED_REFRESH_MS) return;
    _lastDrawMs = millis();

    const OledViewMode view = portalDisplayMode();
    if (view == OledViewMode::AUTO && millis()-_lastPageMs > OLED_PAGE_MS) nextPage();

    uint8_t page = _page;
    if(view == OledViewMode::CLOCK) page = 0;
    else if(view == OledViewMode::SIGNAL) page = 1;
    else if(view == OledViewMode::DECODER) page = 2;
    else if(view == OledViewMode::DIAGNOSTICS) page = 3;

    const auto &s = decoder.stats();
    DCFDateTime dt;
    const bool clock = decoder.getRunningClock(dt);
    const bool accum = decoder.decodeMode() == DecodeMode::ACCUMULATE;

    _oled.clearDisplay();
    _oled.setTextColor(SSD1306_WHITE);
    _oled.setTextSize(1);
    _oled.setCursor(0,0);
    _oled.printf("DCF77 %c %s Q:%u\n",
                 accum?'A':'D', s.minuteSynced?"SYNC":"SEARCH",s.quality);

    if (page == 0) {
        _oled.setTextSize(2);
        if (clock) _oled.printf("%02d:%02d:%02d\n",dt.hour,dt.minute,dt.second);
        else _oled.println("--:--:--");
        _oled.setTextSize(1);
        if (clock) _oled.printf("%02d/%02d/%04d %s\n",dt.day,dt.month,dt.year,dt.cest?"CEST":"CET");
        else _oled.println("Attesa sincronismo");
        _oled.printf("Frame:%lu/%lu\n",(unsigned long)s.validFrames,(unsigned long)s.invalidFrames);
        _oled.println(portalAddress());
    } else if (page == 1) {
        _oled.printf("SEGNALE DCF77\n");
        _oled.printf("Pulse: %.1f ms\n",s.lastPulseWidthUs/1000.0f);
        _oled.printf("Period: %.1f ms\n",s.lastPeriodUs/1000.0f);
        _oled.printf("Bit:%d Conf:%u%%\n",s.lastBit,s.lastBitConfidence);
        _oled.printf("Jit RMS:%.2f ms\n",s.jitterRmsUs/1000.0f);
    } else if (page == 2) {
        _oled.printf("%s\n",accum?"ACCUMULO RADIO":"DECODER DIRETTO");
        _oled.printf("Frame:%u/59\n",s.frameBitCount);
        if (accum) {
            _oled.printf("Cand:%u Campo:%u%%\n",s.candidateMinutes,s.fieldConfidence);
            _oled.printf("Inc:%u Rec:%u\n",s.uncertainBits,s.recoveredBits);
        } else {
            _oled.printf("P1:%s P2:%s P3:%s\n",s.parityMinute?"OK":"--",s.parityHour?"OK":"--",s.parityDate?"OK":"--");
            _oled.printf("Marker:%lu\n",(unsigned long)s.minuteMarkers);
        }
    } else {
        const uint32_t total=s.validPulses+s.invalidPulses;
        const uint8_t ratio=total?static_cast<uint8_t>((s.validPulses*100UL)/total):0;
        _oled.printf("DIAGNOSTICA\n");
        _oled.printf("Valid:%u%% Heap:%uK\n",ratio,ESP.getFreeHeap()/1024U);
        if(WiFi.status()==WL_CONNECTED) _oled.printf("WiFi:%d dBm\n",WiFi.RSSI());
        else _oled.println("WiFi: AP only");
        _oled.printf("Glitch:%lu TKO:%lu\n",(unsigned long)s.glitchCount,(unsigned long)s.timingErrors);
        _oled.printf("Perr:%lu Lock:%s\n",(unsigned long)s.parityErrors,s.clockLocked?"YES":"NO");
    }
    _oled.display();
}
