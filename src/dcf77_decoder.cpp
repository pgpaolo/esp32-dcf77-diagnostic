#include "dcf77_decoder.h"
#include "config.h"
#include <math.h>
#include <string.h>

DCF77Decoder::DCF77Decoder() { reset(); }

void DCF77Decoder::reset() {
    _stats = DecoderStats{};
    _decoded = DCFDateTime{};
    _clockBase = DCFDateTime{};
    _clockBaseValid = false;
    _clockBaseMs = 0;
    _frameCount = 0;
    _lastFrameCount = 0;
    _traceHead = _traceCount = 0;
    _jitterPos = _jitterCount = 0;
    _qualityPos = _qualityCount = 0;
    memset(_frame, -1, sizeof(_frame));
    memset(_lastFrame, -1, sizeof(_lastFrame));
    memset(_trace, 0, sizeof(_trace));
    memset(_jitter, 0, sizeof(_jitter));
    memset(_qualityHistory, 0, sizeof(_qualityHistory));
}

int DCF77Decoder::classify(uint32_t widthUs) const {
    if (widthUs >= DCF_ZERO_MIN_US && widthUs <= DCF_ZERO_MAX_US) return 0;
    if (widthUs >= DCF_ONE_MIN_US && widthUs <= DCF_ONE_MAX_US) return 1;
    return -1;
}

void DCF77Decoder::recordTrace(const RawPulse &pulse, int bit, bool valid, bool minuteGap) {
    PulseTrace &t = _trace[_traceHead];
    t.capturedMs = millis();
    t.widthUs = pulse.widthUs;
    t.periodUs = pulse.periodUs;
    t.bit = static_cast<int8_t>(bit);
    t.valid = valid;
    t.minuteGap = minuteGap;
    _traceHead = (_traceHead + 1U) % TRACE_SIZE;
    if (_traceCount < TRACE_SIZE) ++_traceCount;
}

bool DCF77Decoder::recentPulse(uint8_t newestIndex, PulseTrace &out) const {
    if (newestIndex >= _traceCount) return false;
    int i = static_cast<int>(_traceHead) - 1 - newestIndex;
    while (i < 0) i += TRACE_SIZE;
    out = _trace[i];
    return true;
}

void DCF77Decoder::updateQuality(bool validBit, bool validTiming) {
    const uint8_t q = (validBit && validTiming) ? 100 : (validBit ? 40 : 0);
    _qualityHistory[_qualityPos] = q;
    _qualityPos = (_qualityPos + 1U) % 60U;
    if (_qualityCount < 60) ++_qualityCount;

    uint32_t sum = 0;
    for (uint8_t i = 0; i < _qualityCount; ++i) sum += _qualityHistory[i];
    _stats.quality = _qualityCount ? static_cast<uint8_t>(sum / _qualityCount) : 0;
}

void DCF77Decoder::updateJitter(int32_t jitterUs) {
    _jitter[_jitterPos] = jitterUs;
    _jitterPos = (_jitterPos + 1U) % 60U;
    if (_jitterCount < 60) ++_jitterCount;

    double sum = 0;
    for (uint8_t i = 0; i < _jitterCount; ++i) {
        const double v = _jitter[i];
        sum += v * v;
    }
    _stats.jitterRmsUs = _jitterCount ? sqrt(sum / _jitterCount) : 0.0f;
}

void DCF77Decoder::processPulse(const RawPulse &pulse) {
    _stats.totalPulses++;
    _stats.lastPulseWidthUs = pulse.widthUs;
    _stats.lastPeriodUs = pulse.periodUs;

    const bool minuteGap = pulse.periodUs >= DCF_MINUTE_GAP_MIN_US &&
                           pulse.periodUs <= DCF_MINUTE_GAP_MAX_US;
    const bool normalSecond = pulse.periodUs == 0 ||
                              (pulse.periodUs >= DCF_SECOND_MIN_US &&
                               pulse.periodUs <= DCF_SECOND_MAX_US);

    if (minuteGap) {
        _stats.minuteMarkers++;
        if (_stats.minuteSynced && _frameCount > 0) finalizeFrame(pulse.startUs);

        _stats.minuteSynced = true;
        _frameCount = 0;
        _stats.frameBitCount = 0;
        memset(_frame, -1, sizeof(_frame));
    } else if (pulse.periodUs != 0 && !normalSecond) {
        _stats.timingErrors++;
        // A very long gap means synchronization was genuinely lost.
        if (pulse.periodUs > 3000000UL) {
            _stats.minuteSynced = false;
            _frameCount = 0;
            _stats.frameBitCount = 0;
            memset(_frame, -1, sizeof(_frame));
        }
    }

    const int bit = classify(pulse.widthUs);
    const bool valid = bit >= 0;
    _stats.lastBit = static_cast<int8_t>(bit);

    if (valid) _stats.validPulses++;
    else {
        _stats.invalidPulses++;
        if (pulse.widthUs < 30000UL) _stats.glitchCount++;
    }

    const bool timingOk = normalSecond || minuteGap;
    if (pulse.periodUs != 0) {
        const uint32_t expected = minuteGap ? 2000000UL : 1000000UL;
        _stats.lastJitterUs = static_cast<int32_t>(pulse.periodUs) - static_cast<int32_t>(expected);
        if (timingOk) updateJitter(_stats.lastJitterUs);
    }
    updateQuality(valid, timingOk);

    // The pulse following the missing second-59 pulse is second 0.
    if (_stats.minuteSynced && timingOk && _frameCount < 59) {
        _frame[_frameCount++] = static_cast<int8_t>(bit);
        _stats.frameBitCount = _frameCount;
    }

    recordTrace(pulse, bit, valid, minuteGap);
}

void DCF77Decoder::finalizeFrame(uint32_t minuteStartUs) {
    _lastFrameCount = _frameCount;
    for (uint8_t i = 0; i < 59; ++i) _lastFrame[i] = i < _frameCount ? _frame[i] : -1;

    DCFDateTime dt;
    const bool valid = decodeFrame(dt);
    _stats.lastFrameValid = valid;

    if (!valid) {
        _stats.invalidFrames++;
        if (!(_stats.parityMinute && _stats.parityHour && _stats.parityDate))
            _stats.parityErrors++;
        return;
    }

    _stats.validFrames++;
    _stats.lastValidFrameMs = millis();
    _stats.clockLocked = true;
    _decoded = dt;
    setClockBase(dt, minuteStartUs);
}

bool DCF77Decoder::decodeFrame(DCFDateTime &out) {
    _stats.parityMinute = _stats.parityHour = _stats.parityDate = false;
    if (_frameCount != 59) return false;

    for (uint8_t i = 0; i < 59; ++i)
        if (_frame[i] != 0 && _frame[i] != 1) return false;

    if (_frame[20] != 1) return false;
    if (_frame[17] == _frame[18]) return false;

    _stats.parityMinute = evenParity(_frame, 21, 28);
    _stats.parityHour   = evenParity(_frame, 29, 35);
    _stats.parityDate   = evenParity(_frame, 36, 58);
    if (!_stats.parityMinute || !_stats.parityHour || !_stats.parityDate) return false;

    static const int minPos[] = {21,22,23,24,25,26,27};
    static const int minW[]   = {1,2,4,8,10,20,40};
    static const int hrPos[]  = {29,30,31,32,33,34};
    static const int hrW[]    = {1,2,4,8,10,20};
    static const int dayPos[] = {36,37,38,39,40,41};
    static const int dayW[]   = {1,2,4,8,10,20};
    static const int wdPos[]  = {42,43,44};
    static const int wdW[]    = {1,2,4};
    static const int monPos[] = {45,46,47,48,49};
    static const int monW[]   = {1,2,4,8,10};
    static const int yrPos[]  = {50,51,52,53,54,55,56,57};
    static const int yrW[]    = {1,2,4,8,10,20,40,80};

    out = DCFDateTime{};
    out.minute = weighted(_frame,minPos,minW,7);
    out.hour = weighted(_frame,hrPos,hrW,6);
    out.day = weighted(_frame,dayPos,dayW,6);
    out.weekday = weighted(_frame,wdPos,wdW,3);
    out.month = weighted(_frame,monPos,monW,5);
    out.year = 2000 + weighted(_frame,yrPos,yrW,8);
    out.second = 0;
    out.cest = _frame[17] == 1;
    out.dstChangePending = _frame[16] == 1;
    out.leapSecondPending = _frame[19] == 1;

    if (out.minute > 59 || out.hour > 23 || out.month < 1 || out.month > 12 ||
        out.weekday < 1 || out.weekday > 7 ||
        out.day < 1 || out.day > daysInMonth(out.year,out.month)) return false;

    out.valid = true;
    return true;
}

void DCF77Decoder::setClockBase(const DCFDateTime &dt, uint32_t minuteStartUs) {
    _clockBase = dt;
    const uint32_t ageUs = micros() - minuteStartUs;
    _clockBaseMs = millis() - ageUs / 1000UL;
    _clockBaseValid = true;
}

bool DCF77Decoder::getRunningClock(DCFDateTime &out) const {
    if (!_clockBaseValid) return false;
    out = _clockBase;
    addSeconds(out, (millis() - _clockBaseMs) / 1000UL);
    out.valid = true;
    return true;
}

bool DCF77Decoder::evenParity(const int8_t *bits, int first, int last) {
    int ones = 0;
    for (int i = first; i <= last; ++i) {
        if (bits[i] != 0 && bits[i] != 1) return false;
        ones += bits[i];
    }
    return (ones & 1) == 0;
}

int DCF77Decoder::weighted(const int8_t *bits, const int *pos, const int *weights, size_t n) {
    int value = 0;
    for (size_t i = 0; i < n; ++i) if (bits[pos[i]] == 1) value += weights[i];
    return value;
}

bool DCF77Decoder::isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int DCF77Decoder::daysInMonth(int year, int month) {
    static const int d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return month == 2 && isLeapYear(year) ? 29 : d[month-1];
}

void DCF77Decoder::addSeconds(DCFDateTime &dt, uint32_t seconds) {
    uint32_t total = dt.hour*3600UL + dt.minute*60UL + dt.second + seconds;
    uint32_t days = total / 86400UL;
    total %= 86400UL;
    dt.hour = total / 3600UL;
    total %= 3600UL;
    dt.minute = total / 60UL;
    dt.second = total % 60UL;

    while (days--) {
        if (++dt.weekday > 7) dt.weekday = 1;
        if (++dt.day > daysInMonth(dt.year,dt.month)) {
            dt.day = 1;
            if (++dt.month > 12) { dt.month = 1; ++dt.year; }
        }
    }
}
