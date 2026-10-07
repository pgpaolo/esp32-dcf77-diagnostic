#pragma once
#include <Arduino.h>

struct RawSignalStats {
    uint32_t totalEdges = 0;
    uint32_t totalPulses = 0;
    uint32_t validPulses = 0;
    uint32_t invalidPulses = 0;
    uint32_t minuteGaps = 0;
    uint32_t lastPulseUs = 0;
    uint32_t lastPeriodUs = 0;
    uint32_t lastEdgeAgeMs = 0;
    uint16_t edgesPerSecond = 0;
    int8_t lastBitGuess = -1;
    bool dataLevel = false;
    bool ponLow = true;
};

struct RawPulseSample {
    uint32_t ageMs = 0;
    uint32_t widthUs = 0;
    uint32_t periodUs = 0;
    int8_t bitGuess = -1;
    bool valid = false;
};

void portalBegin();
void portalPoll();
void portalReportRaw(const RawSignalStats &stats);
void portalPushPulse(const RawPulseSample &sample);
const char *portalAddress();
