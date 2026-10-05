#pragma once
#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include "dcf77_decoder.h"

class AnalyzerUI {
public:
    AnalyzerUI();
    void begin();
    void nextPage();
    void draw(const DCF77Decoder &decoder);

private:
    Adafruit_SSD1306 _oled;
    bool _available = false;
    uint8_t _page = 0;
    uint32_t _lastDrawMs = 0;
    uint32_t _lastPageMs = 0;
};
