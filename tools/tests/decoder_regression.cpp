#include <array>
#include <iostream>
#include <stdexcept>
#include "Arduino.h"
#include "dcf77_decoder.h"

// Include the production sampler so tests exercise its actual filtering,
// ISR buffering, PLL, symbol classification and minute accumulator.
#include "../../src/sampled_dcf77.cpp"

uint64_t testMicros = 0;
int testPinLevel = LOW;

void require(bool ok, const char *why) {
    if (!ok) throw std::runtime_error(why);
}

std::array<int8_t, 59> frameFor(int minute, int hour = 12) {
    std::array<int8_t, 59> bits{};
    bits[17] = 1; bits[18] = 0; bits[20] = 1;
    auto field = [&](int first, const std::initializer_list<int> &weights,
                     int value, int parityPos) {
        int ones = 0, pos = first;
        for (int w : weights) {
            int b = w < 10 ? ((value % 10) & w) != 0 : ((value / 10) & (w / 10)) != 0;
            bits[pos++] = b; ones += b;
        }
        if (parityPos >= 0) bits[parityPos] = ones & 1;
    };
    field(21,{1,2,4,8,10,20,40},minute,28);
    field(29,{1,2,4,8,10,20},hour,35);
    field(36,{1,2,4,8,10,20},2,-1);
    field(42,{1,2,4},5,-1);
    field(45,{1,2,4,8,10},10,-1);
    field(50,{1,2,4,8,10,20,40,80},26,-1);
    for (int i=36;i<58;++i) bits[58] ^= bits[i];
    return bits;
}

void sampleMs(bool active, DCF77Decoder &decoder) {
    testPinLevel = active ? HIGH : LOW;
    testMicros += 1000;
    onSampleTimer();
    if (testMicros % 10000 == 0) {
        sampledDcfPoll();
        SampledDcfEvent e;
        while (sampledDcfPopEvent(e))
            decoder.processSampledSymbol(e.secondLocked ? e.secondIndex : 255,
                e.bit, e.confidence, e.minuteMarker, e.markerCandidate, e.startUs);
    }
}

void testCompleteStream(int phaseMs) {
    testMicros = 0; testPinLevel = LOW;
    sampledDcfBegin(13, false);
    DCF77Decoder decoder;
    for (int ms=0;ms<phaseMs;++ms) sampleMs(false,decoder);
    for (int m=0;m<8;++m) {
        auto bits = frameFor(34+m);
        for (int sec=0;sec<60;++sec) {
            int width = sec==59 ? 0 : (bits[sec] ? 200 : 100);
            for (int ms=0;ms<1000;++ms) sampleMs(ms<width,decoder);
        }
    }
    SampledDcfSnapshot snap; sampledDcfSnapshot(snap);
    std::cout << "phase=" << phaseMs << " phaseLock=" << snap.phaseLocked
        << " minuteLock=" << snap.secondLocked << " valid=" << decoder.stats().validFrames
        << " invalid=" << decoder.stats().invalidFrames << '\n';
    require(snap.phaseLocked, "clean phase-shifted stream failed PLL lock");
    require(snap.secondLocked, "clean stream failed minute lock");
    require(decoder.stats().validFrames >= 2, "clean stream did not yield consecutive valid frames");
    require(decoder.stats().clockLocked, "clean stream did not synchronize clock");
    require(decoder.stats().parityMinute && decoder.stats().parityHour && decoder.stats().parityDate,
        "sampled parity indicators were not updated");
}

void testBoundaryGate() {
    uint8_t input[200], output[200];
    for (int phase : {0,20,93,94,95,96,97,99}) {
        for (int i=0;i<200;++i) input[i] = ((i-phase+100)%100)<10 ? 10 : 0;
        buildFilteredWindow(input,output);
        uint8_t edges, blocks, longest;
        filteredRawMetrics(output,edges,blocks,longest);
        require(longest>=7,"100ms pulse split across hardware windows was rejected");
    }
}

void feedFrame(DCF77Decoder &d, const std::array<int8_t,59> &bits,
               uint32_t markerStart, uint8_t confidence=100) {
    for (uint8_t i=0;i<59;++i) d.processSampledSymbol(i,bits[i],confidence,false,false);
    testMicros = markerStart + 1900000;
    d.processSampledSymbol(59,-1,100,true,true,markerStart);
}

void testFrameValidation() {
    auto run = [](std::array<int8_t,59> bits) {
        DCF77Decoder d; d.processSampledSymbol(59,-1,100,true,true);
        feedFrame(d,bits,1000000); return d.stats();
    };
    auto good=frameFor(34);
    auto valid=run(good);
    require(valid.validFrames==1 && valid.parityDate,"valid frame rejected");
    auto bad=good; bad[58]^=1;
    auto rejected=run(bad);
    require(rejected.validFrames==0 && rejected.parityErrors==1,"date parity contradiction accepted");
    bad=good; bad[17]=bad[18]=0;
    require(run(bad).validFrames==0,"contradictory timezone accepted");
    bad=good; bad[20]=0;
    require(run(bad).validFrames==0,"wrong time-information marker accepted");
    bad=good; bad[21]=-1;
    require(run(bad).validFrames==1 && run(bad).recoveredBits>=1,"recoverable missing bit rejected");
    bad=good; bad[17]=-1;
    require(run(bad).validFrames==1,"one missing timezone flag was not recovered");
    bad=good; bad[17]=bad[18]=-1;
    require(run(bad).validFrames==0,"unknown timezone invented");

    DCF77Decoder d; d.processSampledSymbol(59,-1,100,true,true);
    feedFrame(d,frameFor(34),1000000);
    feedFrame(d,frameFor(35),61000000);
    DCFDateTime clock;
    require(d.getRunningClock(clock) && clock.minute==35 && clock.second==0,"clock not anchored to minute phase");
    testMicros=63000000;
    require(d.getRunningClock(clock) && clock.second==1,"clock anchored to processing time instead of true minute start");

    DCF77Decoder partial; partial.processSampledSymbol(59,-1,100,true,true);
    for (uint8_t i=0;i<30;++i) partial.processSampledSymbol(i,good[i],100,false,false);
    partial.processSampledSymbol(255,0,100,false,false);
    require(!partial.stats().minuteSynced && partial.currentFrameCount()==0,"old frame retained after minute lock loss");
}

void testNoiseAndActivity() {
    testMicros=0; testPinLevel=LOW; sampledDcfBegin(13,false);
    DCF77Decoder d;
    for (int ms=0;ms<30000;++ms) sampleMs(ms%100<30,d);
    SampledDcfSnapshot snap; sampledDcfSnapshot(snap);
    require(snap.rawTransitions>0 && snap.rawTransitionAgeMs<100,"raw activity was hidden without decoded symbols");
    require(!snap.phaseLocked && !snap.secondLocked && d.stats().validFrames==0,"fragmented noise falsely locked");
    require(snap.rejectedWindows>0,"rejected noisy windows not counted");
    for (int ms=0;ms<4000;++ms) sampleMs(false,d);
    sampledDcfSnapshot(snap);
    require(snap.rawTransitionAgeMs>=3500,"raw activity did not expire after disconnection");
    require(snap.samples==1000 && snap.windowDurationUs==1000000,"sampling duration/count not reported correctly");
    testMicros += 0x100000000ULL;
    sampledDcfSnapshot(snap);
    require(snap.rawTransitionAgeMs>=4000,"raw activity reappeared after micros() rollover");
}

int main() {
    try {
        testBoundaryGate();
        for (int phase : {0,20,500,940,950,960,990}) testCompleteStream(phase);
        testFrameValidation();
        testNoiseAndActivity();
        std::cout << "PASS: production sampler and decoder regression tests\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n'; return 1;
    }
}
