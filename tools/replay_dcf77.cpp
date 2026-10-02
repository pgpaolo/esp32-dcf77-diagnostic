#include <fstream>
#include <iostream>
#include "Arduino.h"
#include "dcf77_decoder.h"
#include "../src/sampled_dcf77.cpp"
uint64_t testMicros=0;
int testPinLevel=LOW;
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    std::ifstream input(argv[1]); if(!input)return 2;
    sampledDcfBegin(13,false);
    DCF77Decoder decoder;
    char c;
    while(input.get(c)) {
        if(c!='0'&&c!='1')return 2;
        testPinLevel=c=='1'?HIGH:LOW; testMicros+=1000;
        onSampleTimer(); sampledDcfPoll();
        SampledDcfEvent e;
        while(sampledDcfPopEvent(e)) decoder.processSampledSymbol(
            e.secondLocked?e.secondIndex:255,e.bit,e.confidence,e.minuteMarker,e.markerCandidate,e.startUs);
    }
    SampledDcfSnapshot s; sampledDcfSnapshot(s);
    std::cout<<"{\"decoder\":\"project\",\"samples\":"<<testMicros/1000
        <<",\"phaseLocked\":"<<s.phaseLocked<<",\"minuteLocked\":"<<s.secondLocked
        <<",\"validFrames\":"<<decoder.stats().validFrames
        <<",\"clockLocked\":"<<decoder.stats().clockLocked<<"}\n";
}
