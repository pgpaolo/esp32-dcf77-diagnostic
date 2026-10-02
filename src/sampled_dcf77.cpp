#include "sampled_dcf77.h"
#include "config.h"

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
volatile uint32_t captureStartUs = 0;
volatile uint32_t readyStartUs = 0;
volatile uint32_t readyDurationUs = 0;
volatile uint16_t readySamples = 0;
volatile uint32_t rawTransitions = 0;
volatile uint32_t lastRawTransitionMs = 0;
volatile bool previousRawActive = false;

uint8_t previousWindow[BINS] = {};
bool havePreviousWindow = false;
uint32_t previousWindowStartUs = 0;
uint8_t previousWindowPrecedingBin = 0;

// Persistent phase score. This version proved more stable with the MASO-S-R1
// on HW-364A than the direct convolution port, while the downstream symbol
// and 60-second decoders remain Udo-style.
int32_t phaseScore[BINS] = {};
uint8_t currentPhaseBin = 0;
uint8_t currentPhaseQuality = 0;
bool currentPhaseLocked = false;
uint8_t phaseStableSeconds = 0;
uint8_t phaseCandidateBin = 0;
uint8_t phaseCandidateStable = 0;
uint8_t phaseMissedSeconds = 0;
uint32_t lastHandledDroppedWindows = 0;

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
uint32_t rawSyncCandidateCount = 0;

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

inline bool IRAM_ATTR isActiveLevel() {
    const bool high = digitalRead(rxPin) != 0;
    return activeLow ? !high : high;
}

void IRAM_ATTR onSampleTimer() {
    const bool active = isActiveLevel();
    if (active != previousRawActive) {
        previousRawActive = active;
        ++rawTransitions;
        lastRawTransitionMs = millis();
    }
    if (active) {
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
            const uint32_t nowUs = micros();
            readyStartUs = captureStartUs;
            readyDurationUs = nowUs - captureStartUs;
            readySamples = samplesInWindow;
            captureStartUs = nowUs;
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

void buildFilteredWindow(const uint8_t combined[200], uint8_t filtered[200]) {
    // Start from 10 ms boolean activity.
    for (uint16_t i = 0; i < 200; ++i) filtered[i] = combined[i] > 5 ? 10 : 0;

    // 1) Remove isolated one-bin bursts (<=10 ms).
    for (uint16_t i = 1; i < 199; ++i) {
        if (filtered[i] && !filtered[i - 1] && !filtered[i + 1]) filtered[i] = 0;
    }

    // 2) Close gaps up to 20 ms between active regions.
    for (uint16_t i = 1; i < 198; ++i) {
        if (!filtered[i] && filtered[i - 1] && filtered[i + 1]) filtered[i] = 10;
    }
    for (uint16_t i = 1; i < 197; ++i) {
        if (!filtered[i] && !filtered[i + 1] &&
            filtered[i - 1] && filtered[i + 2]) {
            filtered[i] = 10;
            filtered[i + 1] = 10;
        }
    }
}

void filteredRawMetrics(const uint8_t filtered[200],
                        uint8_t &edges,
                        uint8_t &blocks,
                        uint8_t &longestBins) {
    edges = 0;
    blocks = 0;
    longestBins = 0;
    uint8_t run = 0;
    bool wasActive = false;

    // Metrics for the first decoded second only.
    for (uint8_t i = 0; i < 100; ++i) {
        const bool active = filtered[i] != 0;
        if (active && !wasActive && edges < 255) ++edges;
        if (active) {
            if (run < 100) ++run;
            if (run > longestBins) longestBins = run;
        } else {
            if (run >= 3 && blocks < 255) ++blocks;
            run = 0;
        }
        wasActive = active;
    }
    if (run >= 3 && blocks < 255) ++blocks;

    // Complete the pulse which starts in the first hardware window but ends
    // in the second. Do not count unrelated pulses from that second window.
    // For example, a 100 ms pulse at bin 95 is 50+50 ms, not two invalid runs.
    if (run) {
        uint16_t completeRun = run;
        for (uint16_t i = 100; i < 200 && filtered[i]; ++i) ++completeRun;
        if (completeRun > longestBins)
            longestBins = static_cast<uint8_t>(completeRun > 100 ? 100 : completeRun);
        if (run < 3 && completeRun >= 3 && blocks < 255) ++blocks;
    }
}

bool phaseBinActive(const uint8_t combined[200], uint8_t idx) {
    return combined[idx] > 5;
}

uint8_t phaseRunStrength(const uint8_t combined[200], uint8_t start) {
    // Count active 10 ms bins in the first 100 ms after a candidate edge.
    // The 200-sample buffer lets pulses near the hardware-window boundary
    // continue naturally into the following window.
    uint8_t active = 0;
    for (uint8_t k = 0; k < 10; ++k) {
        if (combined[static_cast<uint16_t>(start) + k] > 5) ++active;
    }
    return active;
}

uint8_t circularDistance100(uint8_t a, uint8_t b) {
    int d = abs(static_cast<int>(a) - static_cast<int>(b));
    if (d > 50) d = 100 - d;
    return static_cast<uint8_t>(d);
}

int8_t signedPhaseError100(uint8_t observed, uint8_t predicted) {
    int d = static_cast<int>(observed) - static_cast<int>(predicted);
    if (d > 50) d -= 100;
    if (d < -50) d += 100;
    return static_cast<int8_t>(d);
}

bool candidateRisingEdge(const uint8_t combined[200], uint8_t idx, uint8_t &strength) {
    // At the capture boundary, bin 99 belongs to the END of this window,
    // not its past. Use the actual last bin of the preceding window. Otherwise
    // a pulse tail at bin 0 invents an edge when second 59 has no new pulse.
    const bool previousActive = idx == 0 ? previousWindowPrecedingBin > 5
        : phaseBinActive(combined, static_cast<uint8_t>(idx - 1));
    if (previousActive || !phaseBinActive(combined, idx)) {
        strength = 0;
        return false;
    }

    strength = phaseRunStrength(combined, idx);
    return strength >= 4; // MAS6180B: a valid short pulse may be only 40 ms.
}

// Recognize one uninterrupted pulse near the predicted phase. Only use this
// duration decision when the following 300 ms contain no other active block;
// fragmented signals retain the integrative classifier below. Durations are
// quantized to 10 ms, not precision measurements of the receiver output.
int8_t isolatedPulseBit(const uint8_t filtered[200], uint8_t phase) {
    for (int offset = -3; offset <= 3; ++offset) {
        const int start = static_cast<int>(phase) + offset;
        if (start < 0 || start >= 100 || !filtered[start]) continue;
        const bool preceding = start == 0 ? previousWindowPrecedingBin > 5
                                         : filtered[start - 1] != 0;
        if (preceding) continue;
        int end = start;
        while (end < start + 30 && filtered[end]) ++end;
        if (end == start + 30) continue;
        bool extra = false;
        for (int i = end; i < start + 30; ++i) extra |= filtered[i] != 0;
        if (extra) continue;
        const uint32_t widthUs = static_cast<uint32_t>(end - start) * 10000UL;
        if (widthUs >= DCF_ZERO_MIN_US && widthUs <= DCF_ZERO_MAX_US) return 0;
        if (widthUs >= DCF_ONE_MIN_US && widthUs <= DCF_ONE_MAX_US) return 1;
    }
    return -1;
}

void findPhase(const uint8_t combined[200]) {
    uint8_t filtered[200];
    buildFilteredWindow(combined, filtered);

    uint8_t filteredEdges = 0, filteredBlocks = 0, filteredLongest = 0;
    filteredRawMetrics(filtered, filteredEdges, filteredBlocks, filteredLongest);
    snapshotState.filteredRisingEdges = filteredEdges;
    snapshotState.filteredLongBlocks = filteredBlocks;
    snapshotState.filteredLongestBlockMs = static_cast<uint16_t>(filteredLongest) * 10U;

    // Gate obvious garbage before acquisition/tracking. A useful DCF77 second
    // should expose one dominant pulse after light temporal filtering.
    const bool rawPlausible =
        (filteredEdges <= 3) &&
        (filteredBlocks <= 3) &&
        (filteredLongest >= 4); // >=40 ms; MAS6180B recognition limit, table 5.

    if (!rawPlausible) {
        ++snapshotState.rejectedWindows;
        if (currentPhaseLocked) {
            if (phaseMissedSeconds < 255) ++phaseMissedSeconds;
            if (currentPhaseQuality > 20) currentPhaseQuality -= 20;
            else currentPhaseQuality = 0;
            if (phaseMissedSeconds >= 2) {
                currentPhaseLocked = false;
                phaseStableSeconds = 0;
                phaseCandidateStable = 0;
            }
        } else {
            currentPhaseQuality = 0;
            phaseCandidateStable = 0;
        }
        return;
    }
    // Background acquisition histogram: a real DCF77 pulse start repeats at
    // nearly the same modulo-1-second phase, while noise edges are scattered.
    for (uint8_t i = 0; i < BINS; ++i) {
        phaseScore[i] = (phaseScore[i] * 31) / 32;
    }

    for (uint8_t i = 0; i < BINS; ++i) {
        uint8_t strength = 0;
        if (!candidateRisingEdge(filtered, i, strength)) continue;

        const int32_t weight = static_cast<int32_t>(strength) * 5;
        phaseScore[i] += weight;

        // Give adjacent 10 ms bins a small share so a marginal edge does not
        // cause lock/unlock simply because it moved by one sampling bin.
        const uint8_t prev = i == 0 ? 99 : static_cast<uint8_t>(i - 1);
        const uint8_t next = i == 99 ? 0 : static_cast<uint8_t>(i + 1);
        phaseScore[prev] += weight / 4;
        phaseScore[next] += weight / 4;
    }

    int32_t best = -1;
    int32_t second = -1;
    uint8_t bestIndex = 0;

    for (uint8_t i = 0; i < BINS; ++i) {
        const int32_t v = phaseScore[i];
        if (v > best) {
            second = best;
            best = v;
            bestIndex = i;
        } else if (v > second && circularDistance100(i, bestIndex) > 3) {
            second = v;
        }
    }

    if (second < 0) second = 0;
    const int32_t separation = best > second ? best - second : 0;

    if (!currentPhaseLocked) {
        if (phaseCandidateStable == 0 ||
            circularDistance100(bestIndex, phaseCandidateBin) > 3) {
            phaseCandidateBin = bestIndex;
            phaseCandidateStable = 1;
        } else {
            if (phaseCandidateStable < 255) ++phaseCandidateStable;
        }

        int32_t q = separation / 2;
        if (q > 100) q = 100;
        currentPhaseQuality = static_cast<uint8_t>(q);
        currentPhaseBin = bestIndex;

        // Four coherent seconds are enough for initial lock. The histogram
        // must also have a clear advantage over competing noise phases.
        if (best >= 120 && separation >= 25 && phaseCandidateStable >= 4) {
            currentPhaseBin = phaseCandidateBin;
            currentPhaseLocked = true;
            phaseStableSeconds = 4;
            phaseMissedSeconds = 0;
            if (currentPhaseQuality < 50) currentPhaseQuality = 50;
        }
        return;
    }

    // PLL tracking after lock: inspect only a narrow +/-50 ms aperture around
    // the predicted DCF pulse start. A remote noise edge can never drag phase.
    bool found = false;
    uint8_t observed = currentPhaseBin;
    uint8_t observedStrength = 0;
    uint8_t observedDistance = 255;

    for (int8_t off = -5; off <= 5; ++off) {
        int p = static_cast<int>(currentPhaseBin) + off;
        while (p < 0) p += 100;
        while (p >= 100) p -= 100;
        const uint8_t idx = static_cast<uint8_t>(p);

        uint8_t strength = 0;
        if (!candidateRisingEdge(filtered, idx, strength)) continue;

        const uint8_t distance = circularDistance100(idx, currentPhaseBin);
        if (!found || strength > observedStrength ||
            (strength == observedStrength && distance < observedDistance)) {
            found = true;
            observed = idx;
            observedStrength = strength;
            observedDistance = distance;
        }
    }

    if (found) {
        phaseMissedSeconds = 0;
        if (phaseStableSeconds < 255) ++phaseStableSeconds;

        // First-order digital PLL: correct at most one 10 ms bin per second.
        const int8_t error = signedPhaseError100(observed, currentPhaseBin);
        if (error > 0) {
            currentPhaseBin = currentPhaseBin == 99 ? 0 : currentPhaseBin + 1;
        } else if (error < 0) {
            currentPhaseBin = currentPhaseBin == 0 ? 99 : currentPhaseBin - 1;
        }

        int q = static_cast<int>(observedStrength) * 10;
        if (q > 100) q = 100;
        currentPhaseQuality = static_cast<uint8_t>(q);
        return;
    }

    // One missing pulse is expected every minute (DCF second 59). Keep the
    // predicted phase through that gap. Only repeated misses force reacquire.
    if (phaseMissedSeconds < 255) ++phaseMissedSeconds;
    if (currentPhaseQuality > 15) currentPhaseQuality -= 15;
    else currentPhaseQuality = 0;

    if (phaseMissedSeconds >= 3) {
        currentPhaseLocked = false;
        phaseStableSeconds = 0;
        phaseCandidateStable = 0;
        phaseCandidateBin = bestIndex;
        currentPhaseBin = bestIndex;
    }
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
        rawSyncCandidateCount++;
        snapshotState.rawSyncCandidates = rawSyncCandidateCount;
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
    uint8_t filtered[200];
    buildFilteredWindow(combined, filtered);

    if (!currentPhaseLocked) {
        pendingMarkerCandidate = false;
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

    // The hardware 1-second buffers are not aligned to the DCF77 second.
    // Therefore a minute marker must NOT be detected from the activity of the
    // whole raw buffer. Use only the phase-aligned pulse region below.
    //
    // Udo-style two-half decision around the PLL prediction. Once phase is
    // locked, only a narrow +/-30 ms alignment search is allowed so random
    // bursts elsewhere in the second cannot masquerade as a DCF symbol.
    uint16_t bestStart = currentPhaseBin;
    int bestFirst = -1;
    uint8_t bestFirstCount = 0;
    uint8_t bestSecondCount = 0;

    for (int8_t shift = -3; shift <= 3; ++shift) {
        int candidate = static_cast<int>(currentPhaseBin) + shift;
        // Wrapping the alignment search selects a different DCF second.
        // At phase 0, start 97 would include the next second's bit-0 pulse
        // and turn the missing minute-marker pulse into a false zero.
        if (candidate < 0 || candidate >= 100) continue;

        const uint16_t st = static_cast<uint16_t>(candidate);
        const uint8_t firstCount = majorityActiveBins(filtered, st, 10);
        const uint8_t secondCount = majorityActiveBins(filtered, st + 10, 10);

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
    e.startUs = previousWindowStartUs + static_cast<uint32_t>(currentPhaseBin) * 10000UL;
    e.minuteMarker = false;
    e.markerCandidate = false;

    // Real sync candidate: both phase-aligned 100 ms halves must be quiet.
    // This is evaluated in DCF phase, not against the arbitrary hardware
    // 1-second buffer boundary. The candidate is still provisional and must
    // be confirmed by the following fixed DCF77 bit 0.
    const uint8_t alignedActivity = bestFirstCount + bestSecondCount;
    const bool alignedQuiet = alignedActivity <= 2;
    const int8_t durationBit = isolatedPulseBit(filtered, currentPhaseBin);

    if (durationBit >= 0) {
        e.bit = durationBit;
        e.pulseMs = durationBit ? 200 : 100; // nominal symbol width, not measured
    } else if (alignedQuiet) {
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
    if (durationBit >= 0) {
        conf = 70; // coherent isolated pulse within receiver tolerance limits
    } else if (e.markerCandidate) {
        conf = 100 - static_cast<int>(alignedActivity) * 20;
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
    captureStartUs = micros();
    readyStartUs = readyDurationUs = 0;
    readySamples = 0;
    rawTransitions = 0;
    lastRawTransitionMs = 0;
    previousRawActive = isActiveLevel();
    interrupts();

    memset(previousWindow, 0, sizeof(previousWindow));
    memset(phaseScore, 0, sizeof(phaseScore));
    havePreviousWindow = false;
    previousWindowStartUs = 0;
    previousWindowPrecedingBin = previousRawActive ? 10 : 0;
    currentPhaseBin = 0;
    currentPhaseQuality = 0;
    currentPhaseLocked = false;
    phaseStableSeconds = 0;
    phaseCandidateBin = 0;
    phaseCandidateStable = 0;
    phaseMissedSeconds = 0;
    lastHandledDroppedWindows = 0;
    memset(minuteScore, 0, sizeof(minuteScore));
    rawSecondTick = 0;
    minuteBest = 0;
    minuteQuality = 0;
    minuteScoreMax = 0;
    minuteScoreNoise = 0;
    minutePhaseLocked = false;
    minuteStable = 0;
    syncCandidateCount = 0;
    rawSyncCandidateCount = 0;
    pendingMarkerCandidate = false;
    pendingMarkerRawTick = 0;
    pendingMarkerConfidence = 0;
    snapshotState = SampledDcfSnapshot{};
    eventHead = eventTail = 0;
    previousWasMarker = false;
}

void analyzeRawWindow(const uint8_t bins[BINS]) {
    uint8_t risingEdges = 0;
    uint8_t longBlocks = 0;
    uint8_t longestBins = 0;
    uint8_t run = 0;
    bool wasActive = false;

    for (uint8_t i = 0; i < BINS; ++i) {
        const bool active = bins[i] > 5;

        if (active && !wasActive) {
            if (risingEdges < 255) ++risingEdges;
        }

        if (active) {
            if (run < 100) ++run;
            if (run > longestBins) longestBins = run;
        } else {
            if (run >= 3 && longBlocks < 255) ++longBlocks;
            run = 0;
        }

        wasActive = active;
    }

    if (run >= 3 && longBlocks < 255) ++longBlocks;

    snapshotState.rawRisingEdges = risingEdges;
    snapshotState.rawLongBlocks = longBlocks;
    snapshotState.rawLongestBlockMs = static_cast<uint16_t>(longestBins) * 10U;
}

void sampledDcfPoll() {
    if (!windowReady) return;

    uint8_t current[BINS];
    uint32_t drops;
    uint32_t currentStartUs, durationUs;
    uint16_t actualSamples;
    noInterrupts();
    const uint8_t rb = readyBuffer;
    memcpy(current, (const void*)capture[rb], BINS);
    windowReady = false;
    drops = droppedWindows;
    currentStartUs = readyStartUs;
    durationUs = readyDurationUs;
    actualSamples = readySamples;
    interrupts();

    uint16_t activeMs = 0;
    for (uint8_t i = 0; i < BINS; ++i) activeMs += current[i];

    snapshotState.ready = true;
    memcpy(snapshotState.bins, current, BINS);
    snapshotState.samples = actualSamples;
    snapshotState.windowDurationUs = durationUs;
    snapshotState.activeMs = activeMs;
    snapshotState.secondsObserved++;
    snapshotState.droppedWindows = drops;
    analyzeRawWindow(current);

    const uint32_t newDrops = drops - lastHandledDroppedWindows;
    lastHandledDroppedWindows = drops;

    if (havePreviousWindow && newDrops == 0) {
        uint8_t combined[200];
        memcpy(combined, previousWindow, BINS);
        memcpy(combined + BINS, current, BINS);

        findPhase(combined);
        snapshotState.phaseBin = currentPhaseBin;
        snapshotState.phaseQuality = currentPhaseQuality;
        snapshotState.phaseLocked = currentPhaseLocked;

        classifyPreviousSecond(combined);

        // One contiguous captured second elapsed.
        rawSecondTick = static_cast<uint8_t>((rawSecondTick + 1U) % 60U);
    } else if (havePreviousWindow && newDrops > 0) {
        // previousWindow and current are no longer adjacent. Do not decode a
        // synthetic 200 ms region across the gap. Advance the minute timebase
        // for the pending previous second plus every dropped hardware window.
        pendingMarkerCandidate = false;
        rawSecondTick = static_cast<uint8_t>(
            (rawSecondTick + 1U + (newDrops % 60U)) % 60U);

        // Preserve PLL phase prediction: the 1 kHz hardware timer continued
        // running even though the main loop missed one or more windows.
        if (phaseMissedSeconds < 255) ++phaseMissedSeconds;
        snapshotState.phaseBin = currentPhaseBin;
        snapshotState.phaseQuality = currentPhaseQuality;
        snapshotState.phaseLocked = currentPhaseLocked;
        snapshotState.lastBit = -1;
        snapshotState.lastConfidence = 0;
        snapshotState.lastPulseMs = 0;
    }

    previousWindowPrecedingBin = havePreviousWindow && newDrops == 0
        ? previousWindow[BINS - 1] : 0;
    memcpy(previousWindow, current, BINS);
    previousWindowStartUs = currentStartUs;
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
    noInterrupts();
    const uint32_t transitions = rawTransitions;
    const uint32_t lastMs = lastRawTransitionMs;
    interrupts();
    out.rawTransitions = transitions;
    out.rawTransitionAgeMs = transitions ? millis() - lastMs : UINT32_MAX;
}

#endif
