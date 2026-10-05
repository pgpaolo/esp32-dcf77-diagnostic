#pragma once
#include <Arduino.h>

struct RawPulse {
    uint32_t startUs = 0;
    uint32_t widthUs = 0;
    uint32_t periodUs = 0;
};

struct DCFDateTime {
    int year = 0, month = 0, day = 0, weekday = 0;
    int hour = 0, minute = 0, second = 0;
    bool cest = false;
    bool dstChangePending = false;
    bool leapSecondPending = false;
    bool valid = false;
};

struct PulseTrace {
    uint32_t capturedMs = 0;
    uint32_t widthUs = 0;
    uint32_t periodUs = 0;
    int8_t bit = -1;
    bool valid = false;
    bool minuteGap = false;
};

struct DecoderStats {
    bool minuteSynced = false;
    bool clockLocked = false;
    uint8_t frameBitCount = 0;
    uint32_t totalPulses = 0;
    uint32_t validPulses = 0;
    uint32_t invalidPulses = 0;
    uint32_t minuteMarkers = 0;
    uint32_t validFrames = 0;
    uint32_t invalidFrames = 0;
    uint32_t parityErrors = 0;
    uint32_t timingErrors = 0;
    uint32_t glitchCount = 0;
    int8_t lastBit = -1;
    uint32_t lastPulseWidthUs = 0;
    uint32_t lastPeriodUs = 0;
    int32_t lastJitterUs = 0;
    float jitterRmsUs = 0.0f;
    uint8_t quality = 0;
    bool parityMinute = false;
    bool parityHour = false;
    bool parityDate = false;
    bool lastFrameValid = false;
    uint32_t lastValidFrameMs = 0;
};

class DCF77Decoder {
public:
    DCF77Decoder();
    void reset();
    void processPulse(const RawPulse &pulse);

    const DecoderStats &stats() const { return _stats; }
    const DCFDateTime &decodedTime() const { return _decoded; }
    bool getRunningClock(DCFDateTime &out) const;

    const int8_t *currentFrameBits() const { return _frame; }
    uint8_t currentFrameCount() const { return _frameCount; }
    const int8_t *lastFrameBits() const { return _lastFrame; }
    uint8_t lastFrameCount() const { return _lastFrameCount; }

    uint8_t recentPulseCount() const { return _traceCount; }
    bool recentPulse(uint8_t newestIndex, PulseTrace &out) const;

private:
    DecoderStats _stats;
    DCFDateTime _decoded;
    DCFDateTime _clockBase;
    bool _clockBaseValid = false;
    uint32_t _clockBaseMs = 0;

    int8_t _frame[59];
    uint8_t _frameCount = 0;
    int8_t _lastFrame[59];
    uint8_t _lastFrameCount = 0;

    static constexpr uint8_t TRACE_SIZE = 24;
    PulseTrace _trace[TRACE_SIZE];
    uint8_t _traceHead = 0;
    uint8_t _traceCount = 0;

    int32_t _jitter[60] = {};
    uint8_t _jitterPos = 0;
    uint8_t _jitterCount = 0;
    uint8_t _qualityHistory[60] = {};
    uint8_t _qualityPos = 0;
    uint8_t _qualityCount = 0;

    int classify(uint32_t widthUs) const;
    void updateQuality(bool validBit, bool validTiming);
    void updateJitter(int32_t jitterUs);
    void recordTrace(const RawPulse &pulse, int bit, bool valid, bool minuteGap);
    void finalizeFrame(uint32_t minuteStartUs);
    bool decodeFrame(DCFDateTime &out);
    void setClockBase(const DCFDateTime &dt, uint32_t minuteStartUs);

    static bool evenParity(const int8_t *bits, int first, int last);
    static int weighted(const int8_t *bits, const int *pos, const int *weights, size_t n);
    static bool isLeapYear(int year);
    static int daysInMonth(int year, int month);
    static void addSeconds(DCFDateTime &dt, uint32_t seconds);
};
