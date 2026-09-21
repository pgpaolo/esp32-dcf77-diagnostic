#include "ui.h"
#include "config.h"
#include <math.h>

AnalyzerUI::AnalyzerUI() : _spr(&_tft) {}

void AnalyzerUI::begin() {
    pinMode(PIN_TFT_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_TFT_BACKLIGHT, HIGH);

    _tft.init();
    _tft.setRotation(1);       // 240 x 135 landscape
    _tft.invertDisplay(true);  // required by most classic T-Display panels
    _tft.fillScreen(TFT_BLACK);

    _spr.createSprite(240, 135);
    _spr.setTextWrap(false);
}

void AnalyzerUI::nextPage() {
    _page = (_page + 1) % 4;
    _lastDrawMs = 0;
}

void AnalyzerUI::toggleBacklight() {
    _backlight = !_backlight;
    digitalWrite(PIN_TFT_BACKLIGHT, _backlight ? HIGH : LOW);
}

void AnalyzerUI::draw(const DCF77Decoder &decoder, int analogRaw) {
    if (millis() - _lastDrawMs < DISPLAY_REFRESH_MS) return;
    _lastDrawMs = millis();

    _spr.fillSprite(TFT_BLACK);
    switch (_page) {
        case 0: drawOverview(decoder, analogRaw); break;
        case 1: drawSignal(decoder, analogRaw); break;
        case 2: drawFrame(decoder); break;
        default: drawStats(decoder); break;
    }
    _spr.pushSprite(0, 0);
}

void AnalyzerUI::header(const char *title, const DecoderStats &s) {
    _spr.setTextColor(TFT_CYAN, TFT_BLACK);
    _spr.setTextFont(2);
    _spr.setCursor(4, 2);
    _spr.print(title);

    const bool recentLock = s.clockLocked && s.lastValidFrameMs != 0 &&
                            (millis() - s.lastValidFrameMs) < 180000UL;
    _spr.setTextColor(recentLock ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
    _spr.setCursor(177, 2);
    _spr.print(recentLock ? "LOCK" : "SEARCH");
    _spr.drawFastHLine(0, 19, 240, TFT_DARKGREY);
}

void AnalyzerUI::qualityBar(uint8_t quality, int x, int y, int w, int h) {
    _spr.drawRect(x, y, w, h, TFT_DARKGREY);
    const int fill = (w - 2) * quality / 100;
    uint16_t c = quality >= 75 ? TFT_GREEN : (quality >= 45 ? TFT_YELLOW : TFT_RED);
    if (fill > 0) _spr.fillRect(x + 1, y + 1, fill, h - 2, c);
}

void AnalyzerUI::drawOverview(const DCF77Decoder &decoder, int analogRaw) {
    const DecoderStats &s = decoder.stats();
    header("DCF77 ANALYZER", s);

    DCFDateTime t;
    if (decoder.getRunningClock(t)) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.hour, t.minute, t.second);
        _spr.setTextColor(TFT_WHITE, TFT_BLACK);
        _spr.setTextFont(4);
        _spr.setCursor(38, 25);
        _spr.print(buf);

        _spr.setTextFont(2);
        _spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        snprintf(buf, sizeof(buf), "%s %02d/%02d/%04d  %s",
                 weekdayName(t.weekday), t.day, t.month, t.year, t.cest ? "CEST" : "CET");
        _spr.setCursor(20, 56);
        _spr.print(buf);
    } else {
        _spr.setTextFont(4);
        _spr.setTextColor(TFT_ORANGE, TFT_BLACK);
        _spr.setCursor(47, 30);
        _spr.print("--:--:--");
        _spr.setTextFont(2);
        _spr.setCursor(54, 58);
        _spr.print("attesa frame valido");
    }

    _spr.setTextFont(2);
    _spr.setTextColor(TFT_WHITE, TFT_BLACK);
    _spr.setCursor(5, 82);
    _spr.printf("QUAL %3u%%", s.quality);
    qualityBar(s.quality, 78, 84, 156, 11);

    _spr.setCursor(5, 101);
    _spr.printf("BIT %02u:%c  PULSE %6.1f ms", s.frameBitCount ? s.frameBitCount - 1 : 0,
                s.lastBit < 0 ? '?' : (s.lastBit ? '1' : '0'), s.lastPulseWidthUs / 1000.0f);

    _spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _spr.setCursor(5, 119);
    if (analogRaw >= 0) _spr.printf("ENV %4d   JITTER %+6.2f ms", analogRaw, s.lastJitterUs / 1000.0f);
    else _spr.printf("ENV --     JITTER %+6.2f ms", s.lastJitterUs / 1000.0f);
}

void AnalyzerUI::drawSignal(const DCF77Decoder &decoder, int analogRaw) {
    const DecoderStats &s = decoder.stats();
    header("SIGNAL / TIMING", s);
    _spr.setTextFont(2);

    _spr.setTextColor(s.lastPulseValid ? TFT_GREEN : TFT_RED, TFT_BLACK);
    _spr.setCursor(5, 26);
    _spr.printf("Pulse : %7.2f ms  bit %c", s.lastPulseWidthUs / 1000.0f,
                s.lastBit < 0 ? '?' : (s.lastBit ? '1' : '0'));

    _spr.setTextColor(TFT_WHITE, TFT_BLACK);
    _spr.setCursor(5, 45);
    _spr.printf("Period: %7.2f ms", s.lastPeriodUs / 1000.0f);
    _spr.setCursor(5, 64);
    _spr.printf("Jitter: %+7.2f ms", s.lastJitterUs / 1000.0f);
    _spr.setCursor(5, 83);
    _spr.printf("RMS   : %7.2f ms", s.jitterRmsUs / 1000.0f);

    _spr.setCursor(5, 102);
    if (s.lastPpsOffsetUs == INT32_MIN) _spr.print("PPS off: offset assoluto --");
    else _spr.printf("DCF-PPS: %+8.3f ms", s.lastPpsOffsetUs / 1000.0f);

    _spr.setCursor(5, 120);
    if (analogRaw >= 0) _spr.printf("ENV:%4d  Q:%u%%  ERR:%lu", analogRaw, s.quality, (unsigned long)s.timingErrors);
    else _spr.printf("Q:%u%%  timing err:%lu  glitch:%lu", s.quality,
                     (unsigned long)s.timingErrors, (unsigned long)s.glitchCount);
}

void AnalyzerUI::drawFrame(const DCF77Decoder &decoder) {
    const DecoderStats &s = decoder.stats();
    header("LAST MINUTE FRAME", s);

    const int8_t *bits = decoder.lastFrameBits();
    const uint8_t count = decoder.lastFrameCount();
    const int y = 30;
    for (int i = 0; i < 59; ++i) {
        const int x = 2 + i * 4;
        uint16_t c = TFT_DARKGREY;
        if (i < count) c = bits[i] == 0 ? TFT_CYAN : (bits[i] == 1 ? TFT_YELLOW : TFT_RED);
        _spr.fillRect(x, y, 3, 22, c);
    }

    _spr.setTextFont(1);
    _spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _spr.setCursor(2, 55);
    _spr.print("0          10         20         30         40         50 58");

    _spr.setTextFont(2);
    _spr.setCursor(5, 73);
    _spr.setTextColor(s.parityMinute ? TFT_GREEN : TFT_RED, TFT_BLACK);
    _spr.printf("P1:%s ", s.parityMinute ? "OK" : "ERR");
    _spr.setTextColor(s.parityHour ? TFT_GREEN : TFT_RED, TFT_BLACK);
    _spr.printf("P2:%s ", s.parityHour ? "OK" : "ERR");
    _spr.setTextColor(s.parityDate ? TFT_GREEN : TFT_RED, TFT_BLACK);
    _spr.printf("P3:%s", s.parityDate ? "OK" : "ERR");

    _spr.setTextColor(TFT_WHITE, TFT_BLACK);
    _spr.setCursor(5, 94);
    _spr.printf("bits:%u  valid:%lu  bad:%lu", count,
                (unsigned long)s.validFrames, (unsigned long)s.invalidFrames);

    const DCFDateTime &d = decoder.decodedTime();
    _spr.setCursor(5, 115);
    if (d.valid) {
        _spr.printf("last %02d:%02d  %02d/%02d/%04d", d.hour, d.minute, d.day, d.month, d.year);
    } else {
        _spr.print("nessun frame decodificato");
    }
}

void AnalyzerUI::drawStats(const DCF77Decoder &decoder) {
    const DecoderStats &s = decoder.stats();
    header("STATISTICS", s);
    _spr.setTextFont(2);
    _spr.setTextColor(TFT_WHITE, TFT_BLACK);

    _spr.setCursor(5, 25);
    _spr.printf("Pulse 0 avg: %7.2f ms", s.avgZeroUs / 1000.0f);
    _spr.setCursor(5, 43);
    _spr.printf("Pulse 1 avg: %7.2f ms", s.avgOneUs / 1000.0f);
    _spr.setCursor(5, 61);
    _spr.printf("Period avg : %7.2f ms", s.avgPeriodUs / 1000.0f);
    _spr.setCursor(5, 79);
    _spr.printf("Pulses V/I : %lu/%lu", (unsigned long)s.validPulses, (unsigned long)s.invalidPulses);
    _spr.setCursor(5, 97);
    _spr.printf("Frames V/I : %lu/%lu", (unsigned long)s.validFrames, (unsigned long)s.invalidFrames);
    _spr.setCursor(5, 115);
    _spr.printf("Markers:%lu  parityErr:%lu", (unsigned long)s.minuteMarkers, (unsigned long)s.parityErrors);
}

const char *AnalyzerUI::weekdayName(int weekday) {
    static const char *names[] = {"?", "Lun", "Mar", "Mer", "Gio", "Ven", "Sab", "Dom"};
    if (weekday < 1 || weekday > 7) return "?";
    return names[weekday];
}
