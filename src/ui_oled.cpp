#include "ui.h"
#include "config.h"
#include "web_portal.h"
#include <Wire.h>

AnalyzerUI::AnalyzerUI() : _oled(128,64,&Wire,-1) {}

void AnalyzerUI::begin() {
    Wire.begin(PIN_OLED_SDA,PIN_OLED_SCL);
    _available = _oled.begin(SSD1306_SWITCHCAPVCC,OLED_ADDRESS,true,false);
}

void AnalyzerUI::draw(const RawSignalStats &s) {
    if (!_available || millis()-_lastDrawMs < OLED_REFRESH_MS) return;
    _lastDrawMs = millis();

    _oled.clearDisplay();
    _oled.setTextColor(SSD1306_WHITE);
    _oled.setTextSize(1);
    _oled.setCursor(0,0);

    _oled.println("DCF77 RAW / HW364A");
    _oled.printf("DATA:%s P1:LOW\n",s.dataLevel?"HIGH":"LOW");
    _oled.printf("Edges:%lu  %u/s\n",(unsigned long)s.totalEdges,s.edgesPerSecond);
    _oled.printf("Pulse:%.1f ms\n",s.lastPulseUs/1000.0f);
    _oled.printf("Period:%.1f ms\n",s.lastPeriodUs/1000.0f);
    _oled.printf("Guess:%d  OK:%lu\n",s.lastBitGuess,(unsigned long)s.validPulses);
    _oled.display();
}
