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
    _candidateTime = DCFDateTime{};
    _candidateValid = false;
    _candidateStreak = 0;
    _candidateMisses = 0;

    _traceHead = _traceCount = 0;
    _jitterPos = _jitterCount = 0;
    _qualityPos = _qualityCount = 0;

    memset(_frame, -1, sizeof(_frame));
    memset(_frameConfidence, 0, sizeof(_frameConfidence));
    memset(_lastFrame, -1, sizeof(_lastFrame));
    memset(_lastFrameConfidence, 0, sizeof(_lastFrameConfidence));
    for (uint8_t i = 0; i < TRACE_SIZE; ++i) _trace[i] = PulseTrace{};
    memset(_jitter, 0, sizeof(_jitter));
    memset(_qualityHistory, 0, sizeof(_qualityHistory));
}

void DCF77Decoder::setDecodeMode(DecodeMode mode) {
    if (_mode == mode) return;
    _mode = mode;
    reset();
}

int DCF77Decoder::classify(uint32_t widthUs) const {
    if (widthUs >= DCF_ZERO_MIN_US && widthUs <= DCF_ZERO_MAX_US) return 0;
    if (widthUs >= DCF_ONE_MIN_US && widthUs <= DCF_ONE_MAX_US) return 1;
    return -1;
}

int DCF77Decoder::softClassify(uint32_t widthUs, uint8_t &confidence) const {
    confidence = 0;
    if (widthUs < 40000UL || widthUs > 280000UL) return -1;

    const uint32_t d0 = widthUs > 100000UL ? widthUs - 100000UL : 100000UL - widthUs;
    const uint32_t d1 = widthUs > 200000UL ? widthUs - 200000UL : 200000UL - widthUs;
    const int bit = d0 <= d1 ? 0 : 1;
    const uint32_t err = d0 <= d1 ? d0 : d1;

    if (err > 80000UL) return -1;
    confidence = static_cast<uint8_t>(100UL - (err * 100UL) / 80000UL);
    if (confidence < 20) return -1;
    return bit;
}

void DCF77Decoder::recordTrace(const RawPulse &pulse, int bit, uint8_t confidence,
                               bool valid, bool minuteGap) {
    PulseTrace &t = _trace[_traceHead];
    t.capturedMs = millis();
    t.widthUs = pulse.widthUs;
    t.periodUs = pulse.periodUs;
    t.bit = static_cast<int8_t>(bit);
    t.confidence = confidence;
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

void DCF77Decoder::updateQuality(bool validBit, bool validTiming, uint8_t confidence) {
    uint8_t q = 0;
    if (validBit && validTiming) q = confidence;
    else if (validBit) q = confidence / 2U;

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
        _stats.uncertainBits = 0;
        memset(_frame, -1, sizeof(_frame));
        memset(_frameConfidence, 0, sizeof(_frameConfidence));
    } else if (pulse.periodUs != 0 && !normalSecond) {
        _stats.timingErrors++;
        if (pulse.periodUs > 3000000UL) {
            _stats.minuteSynced = false;
            _frameCount = 0;
            _stats.frameBitCount = 0;
            memset(_frame, -1, sizeof(_frame));
            memset(_frameConfidence, 0, sizeof(_frameConfidence));
        }
    }

    uint8_t confidence = 0;
    int bit = -1;
    if (_mode == DecodeMode::DIRECT) {
        bit = classify(pulse.widthUs);
        confidence = bit >= 0 ? 100 : 0;
    } else {
        bit = softClassify(pulse.widthUs, confidence);
    }

    const bool valid = bit >= 0;
    _stats.lastBit = static_cast<int8_t>(bit);
    _stats.lastBitConfidence = confidence;

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
    updateQuality(valid, timingOk, confidence);

    // The pulse following the missing second-59 pulse is second 0.
    // Even an uncertain pulse occupies its one-second slot in ACCUMULATE mode.
    if (_stats.minuteSynced && timingOk && _frameCount < 59) {
        _frame[_frameCount] = static_cast<int8_t>(bit);
        _frameConfidence[_frameCount] = confidence;
        if (bit < 0 || confidence < 55) {
            if (_stats.uncertainBits < 255) ++_stats.uncertainBits;
        }
        ++_frameCount;
        _stats.frameBitCount = _frameCount;
    }

    recordTrace(pulse, bit, confidence, valid, minuteGap);
}

void DCF77Decoder::finalizeFrame(uint32_t minuteStartUs) {
    _lastFrameCount = _frameCount;
    for (uint8_t i = 0; i < 59; ++i) {
        _lastFrame[i] = i < _frameCount ? _frame[i] : -1;
        _lastFrameConfidence[i] = i < _frameCount ? _frameConfidence[i] : 0;
    }

    DCFDateTime dt;
    uint8_t fieldQ = 0;
    const bool valid = _mode == DecodeMode::DIRECT
        ? decodeFrame(dt)
        : decodeFrameProbabilistic(dt, fieldQ);

    _stats.fieldConfidence = _mode == DecodeMode::DIRECT ? (valid ? 100 : 0) : fieldQ;
    _stats.lastFrameValid = valid;

    if (!valid) {
        _stats.invalidFrames++;
        if (!(_stats.parityMinute && _stats.parityHour && _stats.parityDate))
            _stats.parityErrors++;

        if (_mode == DecodeMode::ACCUMULATE) {
            if (_candidateMisses < 255) ++_candidateMisses;
            if (_candidateMisses > 2 && !_stats.clockLocked) {
                _candidateValid = false;
                _candidateStreak = 0;
                _stats.candidateMinutes = 0;
            }
        }
        return;
    }

    _stats.validFrames++;
    _stats.lastValidFrameMs = millis();

    if (_mode == DecodeMode::DIRECT) {
        _stats.clockLocked = true;
        _stats.candidateMinutes = 1;
        _decoded = dt;
        setClockBase(dt, minuteStartUs);
        return;
    }

    // ACCUMULATE: emulate radio-controlled clocks. A single plausible minute
    // becomes a candidate; synchronization is promoted only after the next
    // decoded minute agrees with a +60 s prediction.
    if (_candidateValid) {
        DCFDateTime expected = _candidateTime;
        addSeconds(expected, 60);
        if (sameMinute(expected, dt)) {
            if (_candidateStreak < 255) ++_candidateStreak;
        } else {
            _candidateStreak = 1;
        }
    } else {
        _candidateStreak = 1;
    }

    _candidateTime = dt;
    _candidateValid = true;
    _candidateMisses = 0;
    _stats.candidateMinutes = _candidateStreak;

    if (_candidateStreak >= 2) {
        _stats.clockLocked = true;
        _decoded = dt;
        setClockBase(dt, minuteStartUs);
    }
}

bool DCF77Decoder::decodeFrame(DCFDateTime &out) {
    _stats.parityMinute = _stats.parityHour = _stats.parityDate = false;
    _stats.recoveredBits = 0;
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

int DCF77Decoder::scoreExpectedBit(int position, int expected) const {
    if (position < 0 || position >= 59) return 0;
    const int8_t observed = _frame[position];
    if (observed != 0 && observed != 1) return 0;
    const int weight = _frameConfidence[position] > 0 ? _frameConfidence[position] : 20;
    return observed == expected ? weight : -weight;
}

bool DCF77Decoder::decodeFrameProbabilistic(DCFDateTime &out, uint8_t &fieldConfidence) {
    fieldConfidence = 0;
    _stats.recoveredBits = 0;
    _stats.parityMinute = _stats.parityHour = _stats.parityDate = false;
    if (_frameCount < 59) return false;

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

    auto bestValue = [&](const int *pos, const int *weights, size_t n,
                         int lo, int hi, int parityPos,
                         int &bestOut, int &marginOut) {
        int bestScore = -32767, secondScore = -32767, bestVal = lo;
        for (int v = lo; v <= hi; ++v) {
            int score = 0;
            int ones = 0;
            for (size_t i = 0; i < n; ++i) {
                const int bit = weights[i] < 10
                    ? (((v % 10) & weights[i]) != 0)
                    : ((((v / 10) & (weights[i] / 10)) != 0));
                ones += bit;
                score += scoreExpectedBit(pos[i], bit);
            }
            if (parityPos >= 0) score += scoreExpectedBit(parityPos, ones & 1);

            if (score > bestScore) {
                secondScore = bestScore;
                bestScore = score;
                bestVal = v;
            } else if (score > secondScore) {
                secondScore = score;
            }
        }
        bestOut = bestVal;
        marginOut = bestScore - secondScore;
        return bestScore;
    };

    int minute, hour, day, weekday, month, year2;
    int mm, hm, dm, wm, mom, ym;
    const int ms  = bestValue(minPos,minW,7,0,59,28,minute,mm);
    const int hs  = bestValue(hrPos,hrW,6,0,23,35,hour,hm);
    const int ds  = bestValue(dayPos,dayW,6,1,31,-1,day,dm);
    const int ws  = bestValue(wdPos,wdW,3,1,7,-1,weekday,wm);
    const int mos = bestValue(monPos,monW,5,1,12,-1,month,mom);
    const int ys  = bestValue(yrPos,yrW,8,0,99,-1,year2,ym);

    // Structural bits: S=1 and exactly one of Z1/Z2 active.
    if (_frame[20] == 0 && _frameConfidence[20] >= 55) return false;
    const int z1 = _frame[17], z2 = _frame[18];
    if ((z1 == 0 || z1 == 1) && (z2 == 0 || z2 == 1) &&
        z1 == z2 && _frameConfidence[17] >= 55 && _frameConfidence[18] >= 55) return false;
    if (z1 < 0 && z2 < 0) return false;

    const bool cest = (z1 == 1) || (z1 < 0 && z2 == 0);

    // Date parity depends on all date fields selected above.
    int dateOnes = 0;
    auto countBits = [&](int v, const int *weights, size_t n) {
        for (size_t i=0;i<n;++i) {
            const int b = weights[i] < 10
                ? (((v % 10) & weights[i]) != 0)
                : ((((v / 10) & (weights[i] / 10)) != 0));
            dateOnes += b;
        }
    };
    countBits(day,dayW,6);
    countBits(weekday,wdW,3);
    countBits(month,monW,5);
    countBits(year2,yrW,8);
    const int dateParityScore = scoreExpectedBit(58, dateOnes & 1);

    // Require each field to stand out from the runner-up.
    if (mm < 15 || hm < 15 || dm < 10 || wm < 10 || mom < 10 || ym < 10) return false;

    const int year = 2000 + year2;
    if (month < 1 || month > 12 || day < 1 || day > daysInMonth(year, month)) return false;

    // Reconstruct expected frame for strong-conflict rejection and recovery count.
    int8_t expected[59];
    memset(expected, -1, sizeof(expected));

    auto encode = [&](const int *pos, const int *weights, size_t n,
                      int value, int parityPos) {
        int ones = 0;
        for (size_t i=0;i<n;++i) {
            const int b = weights[i] < 10
                ? (((value % 10) & weights[i]) != 0)
                : ((((value / 10) & (weights[i] / 10)) != 0));
            expected[pos[i]] = b;
            ones += b;
        }
        if (parityPos >= 0) expected[parityPos] = ones & 1;
    };

    encode(minPos,minW,7,minute,28);
    encode(hrPos,hrW,6,hour,35);
    encode(dayPos,dayW,6,day,-1);
    encode(wdPos,wdW,3,weekday,-1);
    encode(monPos,monW,5,month,-1);
    encode(yrPos,yrW,8,year2,-1);
    expected[58] = dateOnes & 1;
    expected[20] = 1;
    expected[17] = cest ? 1 : 0;
    expected[18] = cest ? 0 : 1;

    uint8_t recovered = 0;
    for (uint8_t i = 0; i < 59; ++i) {
        if (expected[i] < 0 || _frame[i] == expected[i]) continue;
        if ((_frame[i] == 0 || _frame[i] == 1) && _frameConfidence[i] >= 60) return false;
        if (recovered < 255) ++recovered;
    }

    _stats.recoveredBits = recovered;
    _stats.parityMinute = true;
    _stats.parityHour = true;
    _stats.parityDate = dateParityScore >= 0;

    out = DCFDateTime{};
    out.minute = minute;
    out.hour = hour;
    out.day = day;
    out.weekday = weekday;
    out.month = month;
    out.year = year;
    out.second = 0;
    out.cest = cest;
    out.dstChangePending = _frame[16] == 1;
    out.leapSecondPending = _frame[19] == 1;
    out.valid = true;

    const int marginSum = mm + hm + dm + wm + mom + ym;
    const int scoreSum = ms + hs + ds + ws + mos + ys + dateParityScore;
    int q = marginSum / 6 + scoreSum / 100;
    if (q < 0) q = 0;
    if (q > 100) q = 100;
    fieldConfidence = static_cast<uint8_t>(q);
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

bool DCF77Decoder::sameMinute(const DCFDateTime &a, const DCFDateTime &b) {
    return a.year == b.year && a.month == b.month && a.day == b.day &&
           a.hour == b.hour && a.minute == b.minute;
}
