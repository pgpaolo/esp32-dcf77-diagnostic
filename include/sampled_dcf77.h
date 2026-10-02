#pragma once
#include <Arduino.h>

#if defined(ESP8266)

struct SampledDcfEvent {
    int8_t bit = -1;          // 0 / 1 / -1 unknown
    bool minuteMarker = false;
    bool markerCandidate = false; // statistical evidence only until minute phase locks
    uint8_t confidence = 0;
    uint16_t pulseMs = 0;
    uint32_t startUs = 0;
    uint8_t secondIndex = 255; // 0..59 when minute phase is locked
    uint8_t secondQuality = 0;
    bool secondLocked = false;
};

struct SampledDcfSnapshot {
    bool ready = false;
    uint8_t bins[100] = {};
    uint16_t samples = 0;
    uint32_t windowDurationUs = 0;
    uint16_t activeMs = 0;
    uint8_t phaseBin = 0;
    uint8_t phaseQuality = 0;
    bool phaseLocked = false;
    int8_t lastBit = -1;
    bool lastMinuteMarker = false;
    uint8_t lastConfidence = 0;
    uint16_t lastPulseMs = 0;
    uint32_t secondsObserved = 0;
    uint32_t droppedWindows = 0;
    uint8_t secondIndex = 255;
    uint8_t secondQuality = 0;
    bool secondLocked = false;

    // 60-position second decoder diagnostics.
    uint8_t minuteBestCandidate = 0;
    uint8_t minuteScoreMax = 0;
    uint8_t minuteScoreNoise = 0;
    uint8_t minuteLockThreshold = 12;
    uint32_t syncCandidates = 0;
    uint32_t rawSyncCandidates = 0;

    // Raw OUT diagnostics for the last complete 1-second capture.
    uint8_t rawRisingEdges = 0;
    uint8_t rawLongBlocks = 0;       // active blocks >= 30 ms
    uint16_t rawLongestBlockMs = 0;
    uint32_t rawTransitions = 0;
    uint32_t rawTransitionAgeMs = UINT32_MAX;
    uint32_t rejectedWindows = 0;
    uint8_t filteredRisingEdges = 0;
    uint8_t filteredLongBlocks = 0;
    uint16_t filteredLongestBlockMs = 0;
};

void sampledDcfBegin(uint8_t pin, bool activeLow);
void sampledDcfSetPolarity(bool activeLow);
void sampledDcfReset();
void sampledDcfPoll();
bool sampledDcfPopEvent(SampledDcfEvent &out);
void sampledDcfSnapshot(SampledDcfSnapshot &out);

#else

struct SampledDcfEvent {};
struct SampledDcfSnapshot {};
inline void sampledDcfBegin(uint8_t, bool) {}
inline void sampledDcfSetPolarity(bool) {}
inline void sampledDcfReset() {}
inline void sampledDcfPoll() {}
inline bool sampledDcfPopEvent(SampledDcfEvent &) { return false; }
inline void sampledDcfSnapshot(SampledDcfSnapshot &) {}

#endif
