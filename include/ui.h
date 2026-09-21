#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "dcf77_decoder.h"

class AnalyzerUI {
public:
    AnalyzerUI();
    void begin();
    void nextPage();
    void toggleBacklight();
    void draw(const DCF77Decoder &decoder, int analogRaw = -1);

private:
    TFT_eSPI _tft;
    TFT_eSprite _spr;
    uint8_t _page = 0;
    bool _backlight = true;
    uint32_t _lastDrawMs = 0;

    void header(const char *title, const DecoderStats &s);
    void drawOverview(const DCF77Decoder &decoder, int analogRaw);
    void drawSignal(const DCF77Decoder &decoder, int analogRaw);
    void drawFrame(const DCF77Decoder &decoder);
    void drawStats(const DCF77Decoder &decoder);
    void qualityBar(uint8_t quality, int x, int y, int w, int h);
    static const char *weekdayName(int weekday);
};
