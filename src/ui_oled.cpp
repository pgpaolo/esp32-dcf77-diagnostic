#if defined(ESP8266)
#include "ui.h"
#include "config.h"
#include "web_portal.h"
#include "sampled_dcf77.h"
#include <Wire.h>

AnalyzerUI::AnalyzerUI() : _oled(128, 64, &Wire, -1) {}
void AnalyzerUI::begin() {
    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    _available = _oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, true, false);
    if (!_available) Serial.println("OLED initialization failed; portal remains available.");
    _lastPageMs = millis();
}
void AnalyzerUI::nextPage() {
    _page = (_page + 1) % 3;
    _lastPageMs = millis();
}
void AnalyzerUI::toggleBacklight() {
    if (_suspended) return;
    _backlight = !_backlight;
    if (_available) _oled.ssd1306_command(_backlight ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
}
void AnalyzerUI::suspend(bool suspended) {
    _suspended = suspended;
    if (_available) _oled.ssd1306_command(suspended || !_backlight
        ? SSD1306_DISPLAYOFF : SSD1306_DISPLAYON);
}
void AnalyzerUI::draw(const DCF77Decoder &decoder, int, const char *bandLabel, bool ready) {
    if (_suspended || !_available || !_backlight || millis() - _lastDrawMs < 500) return;
    _lastDrawMs = millis();
    if (millis() - _lastPageMs >= OLED_PAGE_MS) nextPage();
    const auto &s = decoder.stats();
    DCFDateTime dt;
    _oled.clearDisplay();
    _oled.setTextColor(SSD1306_WHITE);
    _oled.setTextSize(1);
    _oled.setCursor(0,0);
    _oled.printf("DCF77  %s  %u/3\n", ready ? "RX" : "WAIT", _page+1);
    if (_page == 0) {
        const bool clock = decoder.getRunningClock(dt);
        _oled.setTextSize(2);
        if (clock) _oled.printf("%02d:%02d:%02d\n",dt.hour,dt.minute,dt.second);
        else _oled.println("--:--:--");
        _oled.setTextSize(1);
        if (clock) _oled.printf("%02d/%02d/%04d %s\n",dt.day,dt.month,dt.year,dt.cest?"CEST":"CET");
        else _oled.println("Attesa frame valido");
        SampledDcfSnapshot snap; sampledDcfSnapshot(snap);
        if (snap.phaseLocked) _oled.printf("Confidenza: %u%%\n",s.quality);
        else _oled.println("Confidenza: -- (SEARCH)");
        if (s.validFrames) _oled.printf("Ultimo frame: %lus\n",(unsigned long)((millis()-s.lastValidFrameMs)/1000));
        else _oled.println("Nessun frame valido");
        _oled.println(portalAddress());
    } else if (_page == 1) {
        _oled.println(bandLabel);
        _oled.printf("Impulso: %.1f ms\n",s.lastPulseWidthUs/1000.0f);
        _oled.printf("Periodo: %.1f ms\n",s.lastPeriodUs/1000.0f);
        _oled.printf("Jitter: %.2f ms\n",s.lastJitterUs/1000.0f);
        _oled.printf("RMS: %.2f ms\n",s.jitterRmsUs/1000.0f);
        _oled.printf("Bit %u / valore %d",s.frameBitCount,s.lastBit);
    } else {
        _oled.printf("Frame OK: %lu\n",(unsigned long)s.validFrames);
        _oled.printf("Frame KO: %lu\n",(unsigned long)s.invalidFrames);
        _oled.printf("Parita KO: %lu\n",(unsigned long)s.parityErrors);
        _oled.printf("Impulsi KO: %lu\n",(unsigned long)s.invalidPulses);
        _oled.printf("Glitch: %lu\n",(unsigned long)s.glitchCount);
        _oled.println(portalAddress());
    }
    _oled.display();
}
#endif
