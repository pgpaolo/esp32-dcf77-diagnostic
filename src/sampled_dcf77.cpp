#include "sampled_dcf77.h"

#if defined(ESP8266)
#include <string.h>

namespace {
constexpr uint16_t TIMER1_TICKS_1MS = 5000; // 80 MHz / 16 / 1000
constexpr uint8_t BINS = 100;
constexpr uint8_t SAMPLES_PER_BIN = 10;
constexpr uint8_t EVENT_QUEUE_SIZE = 8;

volatile uint8_t rxPin = 13;
volatile bool activeLow = true;

volatile uint8_t capture[2][BINS];
volatile uint8_t writeBuffer = 0;
volatile uint8_t readyBuffer = 0;
volatile bool windowReady = false;
volatile uint8_t binIndex = 0;
volatile uint8_t samplesInBin = 0;
volatile uint16_t samplesInWindow = 0;
volatile uint32_t droppedWindows = 0;

uint8_t previousWindow[BINS] = {};
bool havePreviousWindow = false;
int32_t phaseScore[BINS] = {};
uint8_t currentPhaseBin = 0;
uint8_t currentPhaseQuality = 0;
bool currentPhaseLocked = false;
uint8_t phaseStableSeconds = 0;

// 60-position minute phase accumulator. A candidate bin represents the raw
// tick that would correspond to DCF second 59 (the missing-pulse marker).
uint8_t minuteScore[60] = {};
uint8_t rawSecondTick = 0;
uint8_t minuteBest = 0;
uint8_t minuteQuality = 0;
bool minutePhaseLocked = false;
uint8_t minuteStable = 0;

SampledDcfSnapshot snapshotState;

SampledDcfEvent eventQueue[EVENT_QUEUE_SIZE];
uint8_t eventHead = 0;
uint8_t eventTail = 0;

bool previousWasMarker = false;
uint32_t syntheticStartUs = 0;

inline bool IRAM_ATTR isActiveLevel() {
    const bool high = digitalRead(rxPin) != 0;
    return activeLow ? !high : high;
}

void IRAM_ATTR onSampleTimer() {
    if (isActiveLevel()) {
        uint8_t &v = const_cast<uint8_t&>(capture[writeBuffer][binIndex]);
        if (v < SAMPLES_PER_BIN) v++;
    }

    samplesInWindow++;
    if (++samplesInBin >= SAMPLES_PER_BIN) {
        samplesInBin = 0;
        if (++binIndex >= BINS) {
            binIndex = 0;

            if (windowReady) droppedWindows++;
            readyBuffer = writeBuffer;
            windowReady = true;

            writeBuffer ^= 1U;
            memset((void*)capture[writeBuffer], 0, BINS);
            samplesInWindow = 0;
        }
    }
}

uint16_t windowSignal(const uint8_t combined[200], uint16_t start, uint16_t len) {
    uint16_t sum = 0;
    for (uint16_t i = 0; i < len; ++i) sum += combined[start + i];
    return sum;
}

void findPhase(const uint8_t combined[200]) {
    int32_t best = -2147483647;
    uint8_t bestIndex = 0;

    // Accumulate the phase evidence over time. Adjacent 10 ms candidates are
    // intentionally allowed to have similar scores; they must NOT be used as
    // the noise reference because a real 100/200 ms pulse naturally spreads
    // over neighbouring candidates.
    for (uint8_t candidate = 0; candidate < BINS; ++candidate) {
        const uint16_t base = candidate;
        const uint16_t a = windowSignal(combined, base, 10);       // 0..100 ms
        const uint16_t b = windowSignal(combined, base + 10, 10);  // 100..200 ms
        const uint16_t n = windowSignal(combined, base + 25, 50);  // 250..750 ms

        const int32_t instant =
            static_cast<int32_t>(2U * a + b) -
            static_cast<int32_t>(n / 2U);

        // Slow integrator: signal phase should build up across many seconds.
        phaseScore[candidate] = (phaseScore[candidate] * 15 + instant * 16) / 16;

        if (phaseScore[candidate] > best) {
            best = phaseScore[candidate];
            bestIndex = candidate;
        }
    }

    currentPhaseBin = bestIndex;

    // Udo-style quality idea: compare the signal phase against a point
    // roughly 200 ms away, not against the adjacent 10 ms bin.
    const uint8_t noiseIndex = static_cast<uint8_t>((bestIndex + 20U) % BINS);
    const int32_t noise = phaseScore[noiseIndex];
    const int32_t separation = best > noise ? best - noise : 0;

    // Scale to a readable 0..100 indicator. The exact value is diagnostic;
    // lock is based on sustained separation, not on one isolated sample.
    int32_t q = separation / 8;
    if (q > 100) q = 100;
    currentPhaseQuality = static_cast<uint8_t>(q);

    if (separation >= 80 && best >= 120) {
        if (phaseStableSeconds < 255) phaseStableSeconds++;
    } else if (separation < 30 || best < 60) {
        phaseStableSeconds = 0;
    } else if (phaseStableSeconds > 0) {
        phaseStableSeconds--;
    }

    currentPhaseLocked = phaseStableSeconds >= 4;
}

uint8_t wrap60(int v) {
    while (v < 0) v += 60;
    while (v >= 60) v -= 60;
    return static_cast<uint8_t>(v);
}

void satAdd(uint8_t &v, uint8_t n) {
    v = (v > 255 - n) ? 255 : static_cast<uint8_t>(v + n);
}

void satSub(uint8_t &v, uint8_t n) {
    v = (v < n) ? 0 : static_cast<uint8_t>(v - n);
}

void updateMinutePhase(const SampledDcfEvent &e) {
    // Udo Klein style sync-mark binning.
    // Scores are persistent and saturating: evidence is accumulated over
    // minutes instead of globally decaying every second.
    const uint8_t current = rawSecondTick;
    const uint8_t previous = wrap60(static_cast<int>(current) - 1);
    const uint8_t previous21 = wrap60(static_cast<int>(current) - 21);
    const uint8_t next = wrap60(static_cast<int>(current) + 1);

    if (e.markerCandidate) {
        // sync mark: +6 current, -2 previous, -2 next, -2 current-21
        satAdd(minuteScore[current], 6);
        satSub(minuteScore[previous], 2);
        satSub(minuteScore[next], 2);
        satSub(minuteScore[previous21], 2);
    } else if (e.bit == 0) {
        // short tick: +1 previous, -2 current, -2 current-21
        satAdd(minuteScore[previous], 1);
        satSub(minuteScore[current], 2);
        satSub(minuteScore[previous21], 2);
    } else if (e.bit == 1) {
        // long tick: +1 current-21, -2 current, -2 previous
        satAdd(minuteScore[previous21], 1);
        satSub(minuteScore[current], 2);
        satSub(minuteScore[previous], 2);
    } else {
        // undefined: penalize all positions directly contradicted by it.
        satSub(minuteScore[current], 2);
        satSub(minuteScore[previous], 2);
        satSub(minuteScore[previous21], 2);
    }

    uint8_t best = 0;
    uint8_t second = 0;
    uint8_t bestIdx = 0;
    for (uint8_t i = 0; i < 60; ++i) {
        const uint8_t v = minuteScore[i];
        if (v >= best) {
            second = best;
            best = v;
            bestIdx = i;
        } else if (v > second) {
            second = v;
        }
    }

    minuteBest = bestIdx;
    minuteQuality = best > second ? static_cast<uint8_t>(best - second) : 0;

    // Udo's second decoder uses a lock threshold of 12 between signal_max
    // and noise_max. Keep the same criterion here.
    minutePhaseLocked = minuteQuality >= 12;
    minuteStable = minutePhaseLocked ? 255 : 0;
}

uint8_t decodedSecondForRawTick(uint8_t rawTick) {
    // minuteBest corresponds to DCF second 59.
    return wrap60(static_cast<int>(rawTick) - static_cast<int>(minuteBest) - 1);
}

void pushEvent(const SampledDcfEvent &e) {
    const uint8_t next = (eventHead + 1U) % EVENT_QUEUE_SIZE;
    if (next == eventTail) return;
    eventQueue[eventHead] = e;
    eventHead = next;
}

int templateScore(const uint8_t combined[200], uint16_t start, uint8_t activeBins) {
    // 10 samples per 10 ms bin. Reward activity where the DCF pulse should
    // exist and reward inactivity after it. This is deliberately tolerant of
    // a noisy receiver and does not require perfect edges.
    int score = 0;
    for (uint8_t i = 0; i < 22; ++i) {
        const uint8_t v = combined[start + i];
        if (i < activeBins) {
            score += static_cast<int>(v) * 2;
        } else {
            score += static_cast<int>(10 - v);
        }
    }

    // Penalize late activity, where a valid DCF77 pulse should already have
    // returned to the idle level.
    for (uint8_t i = 25; i < 55; ++i) {
        score -= static_cast<int>(combined[start + i]) / 2;
    }
    return score;
}

void classifyPreviousSecond(const uint8_t combined[200]) {
    if (!currentPhaseLocked) {
        snapshotState.lastBit = -1;
        snapshotState.lastMinuteMarker = false;
        snapshotState.lastConfidence = 0;
        snapshotState.lastPulseMs = 0;
        snapshotState.secondLocked = minutePhaseLocked;
        snapshotState.secondQuality = minuteQuality;
        snapshotState.secondIndex = minutePhaseLocked
            ? decodedSecondForRawTick(rawSecondTick)
            : 255;
        return;
    }

    // The phase accumulator gives the centre of the useful region, but a real
    // receiver can move the apparent edge by several tens of milliseconds.
    // Search +/-50 ms and compare 100 ms and 200 ms DCF77 templates.
    int best0 = -32768;
    int best1 = -32768;
    uint16_t best0Start = currentPhaseBin;
    uint16_t best1Start = currentPhaseBin;

    for (int8_t shift = -5; shift <= 5; ++shift) {
        int candidate = static_cast<int>(currentPhaseBin) + shift;
        while (candidate < 0) candidate += 100;
        while (candidate >= 100) candidate -= 100;

        // combined[] contains previous+current second, so candidates in the
        // first second can always be evaluated through the following bins.
        const uint16_t st = static_cast<uint16_t>(candidate);
        const int s0 = templateScore(combined, st, 10);
        const int s1 = templateScore(combined, st, 20);

        if (s0 > best0) { best0 = s0; best0Start = st; }
        if (s1 > best1) { best1 = s1; best1Start = st; }
    }

    SampledDcfEvent e;
    e.startUs = syntheticStartUs;
    syntheticStartUs += 1000000UL;
    e.minuteMarker = false;
    e.markerCandidate = false;

    const int best = best0 > best1 ? best0 : best1;
    const int other = best0 > best1 ? best1 : best0;
    const int separation = best - other;

    // A useful DCF symbol must have a recognizable template and some
    // separation between 0 and 1. Weak cases stay '?' and are accumulated.
    if (best >= 180 && separation >= 12) {
        if (best0 > best1) {
            e.bit = 0;
            e.pulseMs = 100;
        } else {
            e.bit = 1;
            e.pulseMs = 200;
        }
        int conf = 35 + separation;
        if (conf > 100) conf = 100;
        e.confidence = static_cast<uint8_t>(conf);
    } else {
        e.bit = -1;
        e.pulseMs = 0;

        // Only weak signal around both templates becomes minute-marker
        // evidence. It is NOT emitted as MIN until the 60-second phase locks.
        if (best < 135) {
            e.markerCandidate = true;
            int conf = 70 - best / 3;
            if (conf < 20) conf = 20;
            if (conf > 80) conf = 80;
            e.confidence = static_cast<uint8_t>(conf);
        } else {
            int conf = separation * 2;
            if (conf > 45) conf = 45;
            e.confidence = static_cast<uint8_t>(conf);
        }
    }

    updateMinutePhase(e);

    e.secondLocked = minutePhaseLocked;
    e.secondQuality = minuteQuality;
    e.secondIndex = minutePhaseLocked ? decodedSecondForRawTick(rawSecondTick) : 255;

    if (e.secondLocked && e.secondIndex == 59) {
        e.minuteMarker = true;
        e.bit = -1;
        e.pulseMs = 0;
        if (e.confidence < 70) e.confidence = 70;
    }

    pushEvent(e);

    snapshotState.lastBit = e.bit;
    snapshotState.lastMinuteMarker = e.minuteMarker;
    snapshotState.lastConfidence = e.confidence;
    snapshotState.lastPulseMs = e.pulseMs;
    snapshotState.secondLocked = e.secondLocked;
    snapshotState.secondQuality = e.secondQuality;
    snapshotState.secondIndex = e.secondIndex;

    rawSecondTick = static_cast<uint8_t>((rawSecondTick + 1U) % 60U);
}

} // namespace

void sampledDcfBegin(uint8_t pin, bool polarityActiveLow) {
    rxPin = pin;
    activeLow = polarityActiveLow;
    sampledDcfReset();

    timer1_isr_init();
    timer1_attachInterrupt(onSampleTimer);
    timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);
    timer1_write(TIMER1_TICKS_1MS);
}

void sampledDcfSetPolarity(bool polarityActiveLow) {
    noInterrupts();
    activeLow = polarityActiveLow;
    interrupts();
    sampledDcfReset();
}

void sampledDcfReset() {
    noInterrupts();
    memset((void*)capture, 0, sizeof(capture));
    writeBuffer = 0;
    readyBuffer = 0;
    windowReady = false;
    binIndex = 0;
    samplesInBin = 0;
    samplesInWindow = 0;
    droppedWindows = 0;
    interrupts();

    memset(previousWindow, 0, sizeof(previousWindow));
    memset(phaseScore, 0, sizeof(phaseScore));
    havePreviousWindow = false;
    currentPhaseBin = 0;
    currentPhaseQuality = 0;
    currentPhaseLocked = false;
    phaseStableSeconds = 0;
    memset(minuteScore, 0, sizeof(minuteScore));
    rawSecondTick = 0;
    minuteBest = 0;
    minuteQuality = 0;
    minutePhaseLocked = false;
    minuteStable = 0;
    snapshotState = SampledDcfSnapshot{};
    eventHead = eventTail = 0;
    previousWasMarker = false;
    syntheticStartUs = micros();
}

void sampledDcfPoll() {
    if (!windowReady) return;

    uint8_t current[BINS];
    uint32_t drops;
    noInterrupts();
    const uint8_t rb = readyBuffer;
    memcpy(current, (const void*)capture[rb], BINS);
    windowReady = false;
    drops = droppedWindows;
    interrupts();

    uint16_t activeMs = 0;
    for (uint8_t i = 0; i < BINS; ++i) activeMs += current[i];

    snapshotState.ready = true;
    memcpy(snapshotState.bins, current, BINS);
    snapshotState.samples = 1000;
    snapshotState.activeMs = activeMs;
    snapshotState.secondsObserved++;
    snapshotState.droppedWindows = drops;

    if (havePreviousWindow) {
        uint8_t combined[200];
        memcpy(combined, previousWindow, BINS);
        memcpy(combined + BINS, current, BINS);

        findPhase(combined);
        snapshotState.phaseBin = currentPhaseBin;
        snapshotState.phaseQuality = currentPhaseQuality;
        snapshotState.phaseLocked = currentPhaseLocked;

        classifyPreviousSecond(combined);
    }

    memcpy(previousWindow, current, BINS);
    havePreviousWindow = true;
}

bool sampledDcfPopEvent(SampledDcfEvent &out) {
    if (eventTail == eventHead) return false;
    out = eventQueue[eventTail];
    eventTail = (eventTail + 1U) % EVENT_QUEUE_SIZE;
    return true;
}

void sampledDcfSnapshot(SampledDcfSnapshot &out) {
    out = snapshotState;
}

#endif
