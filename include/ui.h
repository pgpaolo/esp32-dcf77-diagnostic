#pragma once
#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include "web_portal.h"

class AnalyzerUI {
public:
    AnalyzerUI();
    void begin();
    void draw(const RawSignalStats &stats);

private:
    Adafruit_SSD1306 _oled;
    bool _available = false;
    uint32_t _lastDrawMs = 0;
};
