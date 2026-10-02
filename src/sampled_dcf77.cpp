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
    int32_t second = -2147483647;
    uint8_t bestIndex = 0;

    for (uint8_t candidate = 0; candidate < BINS; ++candidate) {
        const uint16_t base = candidate;
        const uint16_t a = windowSignal(combined, base, 10);      // first 100 ms
        const uint16_t b = windowSignal(combined, base + 10, 10); // next 100 ms
        const uint16_t n = windowSignal(combined, base + 25, 50); // 250..750 ms noise region

        const int32_t instant = static_cast<int32_t>(2U * a + b) - static_cast<int32_t>(n / 2U);
        phaseScore[candidate] = (phaseScore[candidate] * 7 + instant * 16) / 8;

        if (phaseScore[candidate] > best) {
            second = best;
            best = phaseScore[candidate];
            bestIndex = candidate;
        } else if (phaseScore[candidate] > second) {
            second = phaseScore[candidate];
        }
    }

    currentPhaseBin = bestIndex;
    const int32_t separation = best - second;
    const int32_t quality = separation <= 0 ? 0 : separation / 2;
    currentPhaseQuality = quality > 100 ? 100 : static_cast<uint8_t>(quality);

    if (currentPhaseQuality >= 8) {
        if (phaseStableSeconds < 255) phaseStableSeconds++;
    } else {
        if (phaseStableSeconds > 0) phaseStableSeconds--;
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
    // Slow decay prevents stale evidence from dominating forever.
    for (uint8_t i = 0; i < 60; ++i) {
        if (minuteScore[i] > 0) minuteScore[i]--;
    }

    if (e.minuteMarker) {
        // Current raw tick should be DCF second 59.
        satAdd(minuteScore[rawSecondTick], 18);
        satSub(minuteScore[wrap60(rawSecondTick - 1)], 4);
        satSub(minuteScore[wrap60(rawSecondTick + 1)], 4);
    } else if (e.bit == 0) {
        // If this is DCF second 0, marker candidate is one raw tick behind.
        satAdd(minuteScore[wrap60(rawSecondTick - 1)], 3);
        // A zero at DCF second 20 contradicts that candidate.
        satSub(minuteScore[wrap60(rawSecondTick - 21)], 3);
    } else if (e.bit == 1) {
        // DCF second 20 is fixed to 1.
        satAdd(minuteScore[wrap60(rawSecondTick - 21)], 3);
        // A one at DCF second 0 contradicts that candidate.
        satSub(minuteScore[wrap60(rawSecondTick - 1)], 3);
    }

    uint8_t best = 0, second = 0, bestIdx = 0;
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

    // Lock only after repeated separation from competing positions.
    if (minuteQuality >= 10 && best >= 18) {
        if (minuteStable < 255) minuteStable++;
    } else if (minuteStable > 0) {
        minuteStable--;
    }
    minutePhaseLocked = minuteStable >= 4;
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

void classifyPreviousSecond(const uint8_t combined[200]) {
    if (!currentPhaseLocked) return;

    const uint16_t p = currentPhaseBin;
    const uint16_t first110 = windowSignal(combined, p, 11);
    const uint16_t second110 = windowSignal(combined, p + 11, 11);
    const uint16_t noise = windowSignal(combined, p + 25, 50);

    const bool firstActive = first110 >= 55;
    const bool secondActive = second110 >= 55;

    SampledDcfEvent e;
    e.startUs = syntheticStartUs;
    syntheticStartUs += 1000000UL;

    if (!firstActive && !secondActive) {
        e.minuteMarker = true;
        e.bit = -1;
        e.pulseMs = 0;
        e.confidence = noise < 120 ? 90 : 60;
        previousWasMarker = true;
        pushEvent(e);
    } else {
        e.minuteMarker = false;
        e.bit = firstActive ? (secondActive ? 1 : 0) : -1;
        e.pulseMs = e.bit == 0 ? 100 : (e.bit == 1 ? 200 : 0);

        uint16_t signalStrength = first110 + second110;
        uint16_t penalty = noise / 5U;
        int conf = static_cast<int>(signalStrength / 2U) - static_cast<int>(penalty);
        if (e.bit < 0) conf /= 2;
        if (conf < 0) conf = 0;
        if (conf > 100) conf = 100;
        e.confidence = static_cast<uint8_t>(conf);

        pushEvent(e);
        previousWasMarker = false;
    }

    updateMinutePhase(e);

    SampledDcfEvent tagged = e;
    tagged.secondLocked = minutePhaseLocked;
    tagged.secondQuality = minuteQuality;
    tagged.secondIndex = minutePhaseLocked ? decodedSecondForRawTick(rawSecondTick) : 255;

    // Replace the just-enqueued untagged event with a tagged copy.
    if (eventHead != eventTail) {
        const uint8_t last = eventHead == 0 ? EVENT_QUEUE_SIZE - 1 : eventHead - 1;
        eventQueue[last] = tagged;
    }

    snapshotState.lastBit = e.bit;
    snapshotState.lastMinuteMarker = e.minuteMarker;
    snapshotState.lastConfidence = e.confidence;
    snapshotState.lastPulseMs = e.pulseMs;
    snapshotState.secondLocked = minutePhaseLocked;
    snapshotState.secondQuality = minuteQuality;
    snapshotState.secondIndex = tagged.secondIndex;

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
