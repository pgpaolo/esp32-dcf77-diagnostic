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

    snapshotState.lastBit = e.bit;
    snapshotState.lastMinuteMarker = e.minuteMarker;
    snapshotState.lastConfidence = e.confidence;
    snapshotState.lastPulseMs = e.pulseMs;
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
