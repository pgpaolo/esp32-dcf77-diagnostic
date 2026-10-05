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
    _page = (_page + 1U) % 3U;
    _lastPageMs = millis();
}

void AnalyzerUI::draw(const DCF77Decoder &decoder) {
    if (!_available || millis()-_lastDrawMs < OLED_REFRESH_MS) return;
    _lastDrawMs = millis();
    if (millis()-_lastPageMs > OLED_PAGE_MS) nextPage();

    const auto &s = decoder.stats();
    DCFDateTime dt;
    const bool clock = decoder.getRunningClock(dt);

    _oled.clearDisplay();
    _oled.setTextColor(SSD1306_WHITE);
    _oled.setTextSize(1);
    _oled.setCursor(0,0);
    _oled.printf("DCF77 %s Q:%u%% %u/3\n", s.minuteSynced?"SYNC":"SEARCH",s.quality,_page+1);

    if (_page == 0) {
        _oled.setTextSize(2);
        if (clock) _oled.printf("%02d:%02d:%02d\n",dt.hour,dt.minute,dt.second);
        else _oled.println("--:--:--");
        _oled.setTextSize(1);
        if (clock) _oled.printf("%02d/%02d/%04d %s\n",dt.day,dt.month,dt.year,dt.cest?"CEST":"CET");
        else _oled.println("Attesa frame valido");
        _oled.printf("Frame OK:%lu KO:%lu\n",(unsigned long)s.validFrames,(unsigned long)s.invalidFrames);
        _oled.println(portalAddress());
    } else if (_page == 1) {
        _oled.printf("Impulso: %.1f ms\n",s.lastPulseWidthUs/1000.0f);
        _oled.printf("Periodo: %.1f ms\n",s.lastPeriodUs/1000.0f);
        _oled.printf("Bit: %d  pos:%u\n",s.lastBit,s.frameBitCount);
        _oled.printf("Jitter: %.1f ms\n",s.lastJitterUs/1000.0f);
        _oled.printf("Marker: %lu\n",(unsigned long)s.minuteMarkers);
    } else {
        _oled.printf("Impulsi OK:%lu\n",(unsigned long)s.validPulses);
        _oled.printf("Impulsi KO:%lu\n",(unsigned long)s.invalidPulses);
        _oled.printf("Timing KO:%lu\n",(unsigned long)s.timingErrors);
        _oled.printf("Parity KO:%lu\n",(unsigned long)s.parityErrors);
        _oled.printf("Glitch:%lu\n",(unsigned long)s.glitchCount);
        _oled.printf("RMS: %.1f ms\n",s.jitterRmsUs/1000.0f);
    }
    _oled.display();
}
