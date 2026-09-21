#include "dcf77_decoder.h"
#include "config.h"
#include <math.h>
#include <string.h>

DCF77Decoder::DCF77Decoder() {
    reset();
}

void DCF77Decoder::reset() {
    _stats = DecoderStats{};
    _decoded = DCFDateTime{};
    _frameCount = 0;
    _lastFrameCount = 0;
    _qualityPos = _qualityCount = 0;
    _jitterPos = _jitterCount = 0;
    _zeroCount = _oneCount = _periodCount = 0;
    _clockBase = DCFDateTime{};
    _clockBaseMs = 0;
    _clockBaseValid = false;

    memset(_frame, -1, sizeof(_frame));
    memset(_lastFrame, -1, sizeof(_lastFrame));
    memset(_qualityHistory, 0, sizeof(_qualityHistory));
    memset(_jitterHistory, 0, sizeof(_jitterHistory));
}

void DCF77Decoder::setSignalMode(SignalMode mode) {
    if (_mode == mode) return;
    _mode = mode;
    reset();
}

int DCF77Decoder::classifyPulse(uint32_t widthUs) const {
    if (widthUs >= DCF_ZERO_MIN_US && widthUs <= DCF_ZERO_MAX_US) return 0;
    if (widthUs >= DCF_ONE_MIN_US && widthUs <= DCF_ONE_MAX_US) return 1;
    return -1;
}

void DCF77Decoder::processPulse(const RawPulse &pulse) {
    _stats.totalPulses++;
    _stats.lastPulseWidthUs = pulse.widthUs;
    _stats.lastPeriodUs = pulse.periodUs;
    _stats.lastPpsOffsetUs = pulse.ppsOffsetUs;

    // On 60 kHz the receiver can be listening to MSF, WWVB or JJY60.
    // Their modulation is not DCF77-compatible, so only perform neutral
    // pulse/timing diagnostics here.
    if (_mode == SignalMode::RAW_60KHZ) {
        const bool valid = pulse.widthUs >= RAW60_PULSE_MIN_US &&
                           pulse.widthUs <= RAW60_PULSE_MAX_US;
        _stats.lastBit = -1;
        _stats.lastPulseValid = valid;
        if (valid) _stats.validPulses++;
        else {
            _stats.invalidPulses++;
            if (pulse.widthUs < 30000) _stats.glitchCount++;
        }

        bool normalSecond = false;
        if (pulse.periodUs >= DCF_SECOND_MIN_US && pulse.periodUs <= DCF_SECOND_MAX_US) {
            normalSecond = true;
            const int32_t jitter = static_cast<int32_t>(pulse.periodUs) - 1000000;
            _stats.lastJitterUs = jitter;
            updateJitter(jitter);
            _periodCount++;
            _stats.avgPeriodUs += (static_cast<float>(pulse.periodUs) - _stats.avgPeriodUs) / _periodCount;
        } else if (pulse.periodUs != 0) {
            _stats.timingErrors++;
        }

        updateQuality(pulse, -1, valid, normalSecond);
        return;
    }

    const bool candidateMinuteGap = pulse.periodUs >= DCF_MINUTE_GAP_MIN_US &&
                                    pulse.periodUs <= DCF_MINUTE_GAP_MAX_US;
    const bool minuteGap = candidateMinuteGap &&
                           (!_stats.minuteSynced || _frameCount >= 55);

    if (minuteGap) {
        _stats.minuteMarkers++;
        if (_stats.minuteSynced && _frameCount > 0) {
            finalizeFrame(pulse.startUs);
        }
        _stats.minuteSynced = true;
        _frameCount = 0;
        memset(_frame, -1, sizeof(_frame));
    } else if (pulse.periodUs > DCF_SECOND_MAX_US && pulse.periodUs != 0) {
        _stats.timingErrors++;
        if (_stats.minuteSynced) {
            _stats.minuteSynced = false;
            _frameCount = 0;
            memset(_frame, -1, sizeof(_frame));
        }
    }

    const int bit = classifyPulse(pulse.widthUs);
    const bool valid = bit >= 0;
    _stats.lastBit = bit;
    _stats.lastPulseValid = valid;

    if (valid) {
        _stats.validPulses++;
        if (bit == 0) {
            _zeroCount++;
            _stats.avgZeroUs += (static_cast<float>(pulse.widthUs) - _stats.avgZeroUs) / _zeroCount;
        } else {
            _oneCount++;
            _stats.avgOneUs += (static_cast<float>(pulse.widthUs) - _stats.avgOneUs) / _oneCount;
        }
    } else {
        _stats.invalidPulses++;
        if (pulse.widthUs < 30000) _stats.glitchCount++;
    }

    bool normalSecond = false;
    if (pulse.periodUs >= DCF_SECOND_MIN_US && pulse.periodUs <= DCF_SECOND_MAX_US) {
        normalSecond = true;
        const int32_t jitter = static_cast<int32_t>(pulse.periodUs) - 1000000;
        _stats.lastJitterUs = jitter;
        updateJitter(jitter);
        _periodCount++;
        _stats.avgPeriodUs += (static_cast<float>(pulse.periodUs) - _stats.avgPeriodUs) / _periodCount;
    } else if (minuteGap) {
        _stats.lastJitterUs = static_cast<int32_t>(pulse.periodUs) - 2000000;
    }

    updateQuality(pulse, bit, valid, normalSecond || minuteGap);

    if (_stats.minuteSynced) {
        if (_frameCount < sizeof(_frame)) {
            _frame[_frameCount++] = static_cast<int8_t>(bit);
        }
        _stats.frameBitCount = _frameCount;
    }
}

void DCF77Decoder::finalizeFrame(uint32_t newMinuteStartUs) {
    _lastFrameCount = _frameCount > 59 ? 59 : _frameCount;
    for (uint8_t i = 0; i < 59; ++i) {
        _lastFrame[i] = (i < _frameCount) ? _frame[i] : -1;
    }

    DCFDateTime dt;
    bool valid = decodeFrame(dt);
    _stats.lastFrameValid = valid;

    if (valid) {
        _stats.validFrames++;
        _stats.clockLocked = true;
        _stats.lastValidFrameMs = millis();
        _decoded = dt;
        setClockBase(dt, newMinuteStartUs);
    } else {
        _stats.invalidFrames++;
        if (!(_stats.parityMinute && _stats.parityHour && _stats.parityDate)) {
            _stats.parityErrors++;
        }
        if (_stats.lastValidFrameMs != 0 && (millis() - _stats.lastValidFrameMs) > 180000UL) {
            _stats.clockLocked = false;
        }
    }
}

bool DCF77Decoder::decodeFrame(DCFDateTime &out) {
    if (_frameCount < 59) {
        _stats.parityMinute = _stats.parityHour = _stats.parityDate = false;
        return false;
    }

    for (int i = 0; i < 59; ++i) {
        if (_frame[i] != 0 && _frame[i] != 1) {
            _stats.parityMinute = _stats.parityHour = _stats.parityDate = false;
            return false;
        }
    }

    _stats.parityMinute = evenParity(_frame, 21, 28);
    _stats.parityHour   = evenParity(_frame, 29, 35);
    _stats.parityDate   = evenParity(_frame, 36, 58);

    if (_frame[20] != 1 || !_stats.parityMinute || !_stats.parityHour || !_stats.parityDate) {
        return false;
    }

    static const int minPos[] = {21,22,23,24,25,26,27};
    static const int minW[]   = { 1, 2, 4, 8,10,20,40};
    static const int hrPos[]  = {29,30,31,32,33,34};
    static const int hrW[]    = { 1, 2, 4, 8,10,20};
    static const int dayPos[] = {36,37,38,39,40,41};
    static const int dayW[]   = { 1, 2, 4, 8,10,20};
    static const int wdPos[]  = {42,43,44};
    static const int wdW[]    = { 1, 2, 4};
    static const int monPos[] = {45,46,47,48,49};
    static const int monW[]   = { 1, 2, 4, 8,10};
    static const int yrPos[]  = {50,51,52,53,54,55,56,57};
    static const int yrW[]    = { 1, 2, 4, 8,10,20,40,80};

    out.minute = weighted(_frame, minPos, minW, 7);
    out.hour = weighted(_frame, hrPos, hrW, 6);
    out.day = weighted(_frame, dayPos, dayW, 6);
    out.weekday = weighted(_frame, wdPos, wdW, 3);
    out.month = weighted(_frame, monPos, monW, 5);
    out.year = 2000 + weighted(_frame, yrPos, yrW, 8);
    out.second = 0;

    const bool z1 = _frame[17] == 1;
    const bool z2 = _frame[18] == 1;
    if (z1 == z2) return false;
    out.cest = z1 && !z2;
    out.dstChangePending = _frame[16] == 1;
    out.leapSecondPending = _frame[19] == 1;

    if (out.minute > 59 || out.hour > 23 || out.month < 1 || out.month > 12 ||
        out.day < 1 || out.day > daysInMonth(out.year, out.month) ||
        out.weekday < 1 || out.weekday > 7) {
        return false;
    }

    out.valid = true;
    return true;
}

void DCF77Decoder::updateQuality(const RawPulse &pulse, int bit, bool valid, bool normalSecond) {
    float pulseScore = 0.0f;

    if (_mode == SignalMode::RAW_60KHZ) {
        pulseScore = valid ? 1.0f : 0.0f;
    } else if (valid) {
        const float ideal = bit == 0 ? 100000.0f : 200000.0f;
        const float err = fabsf(static_cast<float>(pulse.widthUs) - ideal);
        pulseScore = 1.0f - fminf(err / 60000.0f, 1.0f);
    }

    float periodScore = 0.0f;
    if (pulse.periodUs == 0) {
        periodScore = 0.5f;
    } else if (normalSecond) {
        float ideal = 1000000.0f;
        if (_mode == SignalMode::DCF77 && pulse.periodUs >= DCF_MINUTE_GAP_MIN_US) ideal = 2000000.0f;
        const float err = fabsf(static_cast<float>(pulse.periodUs) - ideal);
        periodScore = 1.0f - fminf(err / 180000.0f, 1.0f);
    }

    float score = (_mode == SignalMode::RAW_60KHZ)
                    ? (0.40f * pulseScore + 0.60f * periodScore)
                    : (0.70f * pulseScore + 0.30f * periodScore);

    _qualityHistory[_qualityPos] = score;
    _qualityPos = (_qualityPos + 1) % 60;
    if (_qualityCount < 60) _qualityCount++;

    float sum = 0.0f;
    for (uint8_t i = 0; i < _qualityCount; ++i) sum += _qualityHistory[i];
    float q = _qualityCount ? (sum / _qualityCount) * 100.0f : 0.0f;

    if (_mode == SignalMode::DCF77) {
        if (_stats.lastFrameValid) q = fminf(100.0f, q + 5.0f);
        if (_stats.invalidFrames > 0 && !_stats.lastFrameValid) q = fmaxf(0.0f, q - 8.0f);
    }
    _stats.quality = static_cast<uint8_t>(lroundf(fmaxf(0.0f, fminf(q, 100.0f))));
}

void DCF77Decoder::updateJitter(int32_t jitterUs) {
    _jitterHistory[_jitterPos] = jitterUs;
    _jitterPos = (_jitterPos + 1) % 60;
    if (_jitterCount < 60) _jitterCount++;

    double sumSq = 0.0;
    for (uint8_t i = 0; i < _jitterCount; ++i) {
        const double j = static_cast<double>(_jitterHistory[i]);
        sumSq += j * j;
    }
    _stats.jitterRmsUs = _jitterCount ? sqrt(sumSq / _jitterCount) : 0.0f;
}

void DCF77Decoder::setClockBase(const DCFDateTime &dt, uint32_t edgeStartUs) {
    _clockBase = dt;
    const uint32_t ageUs = micros() - edgeStartUs;
    _clockBaseMs = millis() - ageUs / 1000UL;
    _clockBaseValid = true;
}

bool DCF77Decoder::getRunningClock(DCFDateTime &out) const {
    if (!_clockBaseValid) return false;
    out = _clockBase;
    const uint32_t elapsedSeconds = (millis() - _clockBaseMs) / 1000UL;
    addSeconds(out, elapsedSeconds);
    out.valid = true;
    return true;
}

bool DCF77Decoder::evenParity(const int8_t *bits, int first, int lastInclusive) {
    int ones = 0;
    for (int i = first; i <= lastInclusive; ++i) {
        if (bits[i] != 0 && bits[i] != 1) return false;
        ones += bits[i];
    }
    return (ones % 2) == 0;
}

int DCF77Decoder::weighted(const int8_t *bits, const int *positions, const int *weights, size_t n) {
    int value = 0;
    for (size_t i = 0; i < n; ++i) {
        if (bits[positions[i]] == 1) value += weights[i];
    }
    return value;
}

bool DCF77Decoder::isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int DCF77Decoder::daysInMonth(int year, int month) {
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && isLeapYear(year)) return 29;
    return days[month - 1];
}

void DCF77Decoder::addSeconds(DCFDateTime &dt, uint32_t seconds) {
    uint32_t total = static_cast<uint32_t>(dt.hour) * 3600UL +
                     static_cast<uint32_t>(dt.minute) * 60UL +
                     static_cast<uint32_t>(dt.second) + seconds;

    uint32_t extraDays = total / 86400UL;
    total %= 86400UL;
    dt.hour = total / 3600UL;
    total %= 3600UL;
    dt.minute = total / 60UL;
    dt.second = total % 60UL;

    while (extraDays--) {
        dt.day++;
        dt.weekday++;
        if (dt.weekday > 7) dt.weekday = 1;
        if (dt.day > daysInMonth(dt.year, dt.month)) {
            dt.day = 1;
            dt.month++;
            if (dt.month > 12) {
                dt.month = 1;
                dt.year++;
            }
        }
    }
}
