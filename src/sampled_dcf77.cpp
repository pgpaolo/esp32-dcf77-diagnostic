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

// Persistent phase score. This version proved more stable with the MASO-S-R1
// on HW-364A than the direct convolution port, while the downstream symbol
// and 60-second decoders remain Udo-style.
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
uint8_t minuteScoreMax = 0;
uint8_t minuteScoreNoise = 0;
bool minutePhaseLocked = false;
uint8_t minuteStable = 0;
uint32_t syncCandidateCount = 0;

// A quiet second is only a provisional marker. DCF77 second 59 is followed
// by second 0, whose bit is fixed to 0. Confirm the marker only when the next
// decoded symbol is a reasonably confident zero.
bool pendingMarkerCandidate = false;
uint8_t pendingMarkerRawTick = 0;
uint8_t pendingMarkerConfidence = 0;

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

    for (uint8_t candidate = 0; candidate < BINS; ++candidate) {
        const uint16_t base = candidate;
        const uint16_t a = windowSignal(combined, base, 10);       // 0..100 ms
        const uint16_t b = windowSignal(combined, base + 10, 10);  // 100..200 ms
        const uint16_t n = windowSignal(combined, base + 25, 50);  // 250..750 ms

        const int32_t instant =
            static_cast<int32_t>(2U * a + b) -
            static_cast<int32_t>(n / 2U);

        // Long-memory integrator. Keep evidence across many seconds but do
        // not let one noisy second move the detected phase abruptly.
        phaseScore[candidate] = (phaseScore[candidate] * 15 + instant * 16) / 16;

        if (phaseScore[candidate] > best) {
            best = phaseScore[candidate];
            bestIndex = candidate;
        }
    }

    currentPhaseBin = bestIndex;

    // Compare against a phase well outside the 100/200 ms useful pulse.
    const uint8_t noiseIndex = static_cast<uint8_t>((bestIndex + 20U) % BINS);
    const int32_t noise = phaseScore[noiseIndex];
    const int32_t separation = best > noise ? best - noise : 0;

    int32_t q = separation / 8;
    if (q > 100) q = 100;
    currentPhaseQuality = static_cast<uint8_t>(q);

    if (separation >= 80 && best >= 120) {
        if (phaseStableSeconds < 255) ++phaseStableSeconds;
    } else if (separation < 30 || best < 60) {
        phaseStableSeconds = 0;
    } else if (phaseStableSeconds > 0) {
        --phaseStableSeconds;
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

void applyMinuteScore(uint8_t tick, int8_t bit, bool confirmedSync) {
    // Udo Klein style sync-mark binning, but a sync mark reaches this
    // function only after confirmation by the following fixed zero bit.
    const uint8_t current = tick;
    const uint8_t previous = wrap60(static_cast<int>(current) - 1);
    const uint8_t previous21 = wrap60(static_cast<int>(current) - 21);
    const uint8_t next = wrap60(static_cast<int>(current) + 1);

    if (confirmedSync) {
        syncCandidateCount++;
        satAdd(minuteScore[current], 6);
        satSub(minuteScore[previous], 2);
        satSub(minuteScore[next], 2);
        satSub(minuteScore[previous21], 2);
    } else if (bit == 0) {
        satAdd(minuteScore[previous], 1);
        satSub(minuteScore[current], 2);
        satSub(minuteScore[previous21], 2);
    } else if (bit == 1) {
        satAdd(minuteScore[previous21], 1);
        satSub(minuteScore[current], 2);
        satSub(minuteScore[previous], 2);
    } else {
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
    minuteScoreMax = best;
    minuteScoreNoise = second;
    minuteQuality = best > second ? static_cast<uint8_t>(best - second) : 0;

    snapshotState.minuteBestCandidate = minuteBest;
    snapshotState.minuteScoreMax = minuteScoreMax;
    snapshotState.minuteScoreNoise = minuteScoreNoise;
    snapshotState.minuteLockThreshold = 12;
    snapshotState.syncCandidates = syncCandidateCount;

    minutePhaseLocked = minuteQuality >= 12;
    minuteStable = minutePhaseLocked ? 255 : 0;
}

void updateMinutePhase(const SampledDcfEvent &e) {
    // Confirm the PREVIOUS quiet second as sync only when this second is the
    // known DCF77 bit-0 and has useful confidence.
    if (pendingMarkerCandidate) {
        const bool confirmed = (e.bit == 0 && !e.markerCandidate && e.confidence >= 55);
        if (confirmed) {
            applyMinuteScore(pendingMarkerRawTick, -1, true);
        } else {
            // A random dropout must not earn sync points. Treat it merely as
            // undefined evidence at its original position.
            applyMinuteScore(pendingMarkerRawTick, -1, false);
        }
        pendingMarkerCandidate = false;
    }

    if (e.markerCandidate) {
        pendingMarkerCandidate = true;
        pendingMarkerRawTick = rawSecondTick;
        pendingMarkerConfidence = e.confidence;
        // Do not score the quiet second yet. The next second decides.
        return;
    }

    applyMinuteScore(rawSecondTick, e.bit, false);
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

uint8_t majorityActiveBins(const uint8_t combined[200], uint16_t start, uint8_t count) {
    uint8_t active = 0;
    for (uint8_t i = 0; i < count; ++i) {
        // stage-1 equivalent: each 10 ms bin is reduced to one boolean by
        // majority vote of its ten 1 ms samples.
        if (combined[start + i] > 5) active++;
    }
    return active;
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

    // First measure the complete second. A DCF77 sync mark is the absence of
    // the normal 100/200 ms pulse, so a second with substantial activity must
    // never be promoted to SYNC merely because our 200 ms window was offset.
    uint16_t wholeSecondActivity = 0;
    for (uint8_t i = 0; i < 100; ++i) wholeSecondActivity += combined[i];

    // Udo-style two-half decision, but allow the MASO apparent edge to move
    // around the accumulated phase. Search +/-80 ms and keep the alignment
    // with the strongest first 100 ms half.
    uint16_t bestStart = currentPhaseBin;
    int bestFirst = -1;
    uint8_t bestFirstCount = 0;
    uint8_t bestSecondCount = 0;

    for (int8_t shift = -8; shift <= 8; ++shift) {
        int candidate = static_cast<int>(currentPhaseBin) + shift;
        while (candidate < 0) candidate += 100;
        while (candidate >= 100) candidate -= 100;

        const uint16_t st = static_cast<uint16_t>(candidate);
        const uint8_t firstCount = majorityActiveBins(combined, st, 10);
        const uint8_t secondCount = majorityActiveBins(combined, st + 10, 10);

        // Prefer a clear active first half. Tie-break toward less late
        // activity so a 100 ms pulse is not shifted into the second half.
        const int score = static_cast<int>(firstCount) * 20 -
                          static_cast<int>(secondCount);
        if (score > bestFirst) {
            bestFirst = score;
            bestStart = st;
            bestFirstCount = firstCount;
            bestSecondCount = secondCount;
        }
    }

    const bool firstActive  = bestFirstCount > 5;
    const bool secondActive = bestSecondCount > 5;

    SampledDcfEvent e;
    e.startUs = syntheticStartUs;
    syntheticStartUs += 1000000UL;
    e.minuteMarker = false;
    e.markerCandidate = false;

    // Real sync candidate: require the WHOLE second to be nearly quiet.
    // At 1 kHz the nominal marker can still contain a few noisy samples, but
    // anything resembling a 100 ms pulse (e.g. ~100 active samples) is not sync.
    const bool wholeSecondQuiet = wholeSecondActivity < 35;

    if (wholeSecondQuiet) {
        e.bit = -1;
        e.pulseMs = 0;
        e.markerCandidate = true;
    } else if (firstActive && secondActive) {
        e.bit = 1;
        e.pulseMs = 200;
    } else if (firstActive && !secondActive) {
        e.bit = 0;
        e.pulseMs = 100;
    } else {
        e.bit = -1;
        e.pulseMs = 0;
    }

    // Confidence from the selected 100/200 ms pattern.
    int conf = 0;
    if (e.markerCandidate) {
        conf = 100 - static_cast<int>(wholeSecondActivity) * 2;
        if (conf < 20) conf = 20;
    } else if (e.bit == 0) {
        conf = static_cast<int>(bestFirstCount) * 10 -
               static_cast<int>(bestSecondCount) * 5;
    } else if (e.bit == 1) {
        conf = (static_cast<int>(bestFirstCount) +
                static_cast<int>(bestSecondCount)) * 5;
    } else {
        const int d1 = abs(static_cast<int>(bestFirstCount) - 5);
        const int d2 = abs(static_cast<int>(bestSecondCount) - 5);
        conf = (d1 + d2) * 4;
        if (conf > 45) conf = 45;
    }

    if (conf < 0) conf = 0;
    if (conf > 100) conf = 100;
    e.confidence = static_cast<uint8_t>(conf);

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
    minuteScoreMax = 0;
    minuteScoreNoise = 0;
    minutePhaseLocked = false;
    minuteStable = 0;
    syncCandidateCount = 0;
    pendingMarkerCandidate = false;
    pendingMarkerRawTick = 0;
    pendingMarkerConfidence = 0;
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
