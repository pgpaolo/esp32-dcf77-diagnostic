#include "Arduino.h"
#include "dcf77_decoder.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
uint64_t testMicros=0;
int testPinLevel=0;
void require(bool ok,const char *why) {if(!ok) throw std::runtime_error(why);}
std::vector<uint8_t> signal(int phase,bool badParity=false) {
    std::vector<uint8_t> result(phase,0);
    for(int minute=34;minute<39;++minute) {
        std::array<int,60> a{},b{};
        auto put=[&](int start,std::initializer_list<int> weights,int value) {
            for(int w:weights) a[start++]=w<10 ? ((value%10)&w)!=0 : ((value/10)&(w/10))!=0;
        };
        put(17,{80,40,20,10,8,4,2,1},26); put(25,{10,8,4,2,1},10);
        put(30,{20,10,8,4,2,1},2); put(36,{4,2,1},5);
        put(39,{20,10,8,4,2,1},12); put(45,{40,20,10,8,4,2,1},minute);
        for(int i=53;i<=58;++i) a[i]=1;
        b[1]=1; b[58]=1;
        auto parity=[&](int first,int last,int pos){int ones=0;for(int i=first;i<=last;++i)ones+=a[i];b[pos]=1-ones%2;};
        parity(17,24,54);parity(25,35,55);parity(36,38,56);parity(39,51,57);
        if(badParity)b[54]^=1;
        for(int second=0;second<60;++second) for(int ms=0;ms<1000;++ms)
            result.push_back(second==0 ? ms<500 : ms<100 ? 1 : ms<200 ? a[second] : ms<300 ? b[second] : 0);
    }
    return result;
}
void feed(DCF77Decoder &d,const std::vector<uint8_t> &samples,uint64_t base=0) {
    for(size_t start=0;start+1000<=samples.size();start+=1000) {
        uint8_t bins[100]={};
        for(int i=0;i<1000;++i) bins[i/10]+=samples[start+i];
        testMicros=base+(start+1000)*1000ULL;
        d.processMsfWindow(bins,static_cast<uint32_t>(base+start*1000ULL));
    }
}
int main() {
    for(int phase:{0,20,370,940,990}) {
        DCF77Decoder d;d.setSignalMode(SignalMode::MSF_60KHZ);
        feed(d,signal(phase),phase==990 ? 4294000000ULL : 0);
        require(d.stats().validFrames>=2,"MSF valid frames missing");
        require(d.stats().clockLocked,"MSF clock not confirmed");
        auto time=d.decodedTime();
        require(time.year==2026 && time.month==10 && time.day==2 && time.hour==12 && time.cest,"MSF date/BST incorrect");
        require(d.stats().msfPhaseLocked,"MSF phase not locked");
        uint8_t bins[100]={};
        testMicros+=3000000;
        d.processMsfWindow(bins,static_cast<uint32_t>(testMicros));
        require(!d.stats().msfPhaseLocked,"Missing windows preserved phase lock");
    }
    DCF77Decoder bad;bad.setSignalMode(SignalMode::MSF_60KHZ);feed(bad,signal(370,true));
    require(bad.stats().validFrames==0 && !bad.stats().clockLocked,"Bad parity accepted");
    require(bad.stats().parityErrors>0,"Bad parity not reported");
    DCF77Decoder constant;constant.setSignalMode(SignalMode::MSF_60KHZ);
    feed(constant,std::vector<uint8_t>(180000,0));
    require(!constant.stats().clockLocked && !constant.stats().msfPhaseLocked,"Constant input acquired");
    std::cout<<"PASS: production MSF phase, split A/B, date/BST, parity, missing windows and micros rollover\n";
}
