#pragma once

#include <Arduino.h>

enum class SignalMode : uint8_t {
    DCF77,
    RAW_60KHZ
};

struct RawPulse {
    uint32_t startUs = 0;
    uint32_t widthUs = 0;
    uint32_t periodUs = 0;
    int32_t ppsOffsetUs = INT32_MIN;
};

struct PulseTrace {
    uint32_t capturedMs = 0;
    uint32_t widthUs = 0;
    uint32_t periodUs = 0;
    int8_t bit = -1;
    bool valid = false;
    bool secondTimingOk = false;
    bool minuteGap = false;
    uint8_t framePos = 0;
};

struct DCFDateTime {
    int year = 0;
    int month = 0;
    int day = 0;
    int weekday = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    bool cest = false;
    bool dstChangePending = false;
    bool leapSecondPending = false;
    bool valid = false;
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

    int lastBit = -1;
    bool lastPulseValid = false;
    uint32_t lastPulseWidthUs = 0;
    uint32_t lastPeriodUs = 0;
    int32_t lastJitterUs = 0;
    int32_t lastPpsOffsetUs = INT32_MIN;

    float avgZeroUs = 0.0f;
    float avgOneUs = 0.0f;
    float avgPeriodUs = 0.0f;
    float jitterRmsUs = 0.0f;
    uint8_t quality = 0;

    bool parityMinute = false;
    bool parityHour = false;
    bool parityDate = false;
    bool lastFrameValid = false;

    uint32_t lastValidFrameMs = 0;

    // Integrative acquisition state.
    uint8_t candidateMinutes = 0;
    uint8_t recoveredBits = 0;
    uint8_t uncertainBits = 0;
    uint8_t acquisitionConfidence = 0;
    uint8_t fieldConfidence = 0;
    uint8_t predictionMatch = 0;
    uint32_t sampledSymbols = 0;
};

class DCF77Decoder {
public:
    DCF77Decoder();

    void reset();
    void setSignalMode(SignalMode mode);
    SignalMode signalMode() const { return _mode; }

    void processPulse(const RawPulse &pulse);
    void processSampledSymbol(uint8_t secondIndex, int8_t bit, uint8_t confidence, bool minuteMarker);
    const DecoderStats &stats() const { return _stats; }
    const DCFDateTime &decodedTime() const { return _decoded; }

    bool getRunningClock(DCFDateTime &out) const;
    const int8_t *currentFrameBits() const { return _frame; }
    uint8_t currentFrameCount() const { return _frameCount; }
    const char *acquisitionState() const;
    const int8_t *lastFrameBits() const { return _lastFrame; }
    uint8_t lastFrameCount() const { return _lastFrameCount; }

    uint8_t recentPulseCount() const { return _pulseTraceCount; }
    bool recentPulse(uint8_t newestIndex, PulseTrace &out) const;

private:
    SignalMode _mode = SignalMode::DCF77;
    DecoderStats _stats;
    DCFDateTime _decoded;

    int8_t _frame[61];
    uint8_t _frameConfidence[61];
    uint8_t _frameCount = 0;
    int8_t _lastFrame[59];
    uint8_t _lastFrameCount = 0;

    float _qualityHistory[60];
    uint8_t _qualityPos = 0;
    uint8_t _qualityCount = 0;

    int32_t _jitterHistory[60];
    uint8_t _jitterPos = 0;
    uint8_t _jitterCount = 0;

    static constexpr uint8_t PULSE_TRACE_SIZE = 24;
    PulseTrace _pulseTrace[PULSE_TRACE_SIZE];
    uint8_t _pulseTraceHead = 0;
    uint8_t _pulseTraceCount = 0;

    uint32_t _zeroCount = 0;
    uint32_t _oneCount = 0;
    uint32_t _periodCount = 0;

    DCFDateTime _clockBase;
    uint32_t _clockBaseMs = 0;
    bool _clockBaseValid = false;

    DCFDateTime _candidateTime;
    bool _candidateValid = false;
    uint8_t _candidateStreak = 0;
    uint8_t _candidateMisses = 0;

    int classifyPulse(uint32_t widthUs) const;
    int softClassifyPulse(uint32_t widthUs, uint8_t &confidence) const;
    void recordPulseTrace(const RawPulse &pulse, int bit, bool valid,
                          bool secondTimingOk, bool minuteGap);
    void finalizeFrame(uint32_t newMinuteStartUs);
    bool decodeFrame(DCFDateTime &out);
    bool decodeFrameProbabilistic(DCFDateTime &out, uint8_t &confidence);
    int scoreExpectedBit(int position, int expected) const;
    int scoreFieldValue(const int *positions, const int *weights, size_t n, int value, int parityPos) const;
    void updateQuality(const RawPulse &pulse, int bit, bool valid, bool normalSecond);
    void updateJitter(int32_t jitterUs);
    void setClockBase(const DCFDateTime &dt, uint32_t edgeStartUs);

    static bool evenParity(const int8_t *bits, int first, int lastInclusive);
    static int weighted(const int8_t *bits, const int *positions, const int *weights, size_t n);
    static bool isLeapYear(int year);
    static int daysInMonth(int year, int month);
    static void addSeconds(DCFDateTime &dt, uint32_t seconds);
    static bool sameMinute(const DCFDateTime &a, const DCFDateTime &b);
};
