#include "dcf77_decoder.h"
#include <string.h>

// Independent implementation of NPL's MSF time/date code. Input is the
// same timer-acquired 100 x 10 ms bins as the scope, without DCF filtering.
void DCF77Decoder::resetMsf() {
    memset(_msfPrevious,0,sizeof(_msfPrevious));
    for (auto &s : _msfScores) s=500;
    memset(_msfB,-1,sizeof(_msfB));
    _msfHavePrevious=_msfCollecting=false;
    _msfStable=_msfMisses=0;
    _msfPreviousUs=_msfCandidateUs=0;
}

bool DCF77Decoder::decodeMsfFrame(DCFDateTime &out) {
    for (int i=1;i<60;++i)
        if (_frame[i]<0 || _msfB[i]<0) return false;
    const int identifier[]={0,1,1,1,1,1,1,0};
    for (int i=0;i<8;++i) if (_frame[52+i]!=identifier[i]) return false;
    auto odd=[&](int first,int last,int parity) {
        int ones=_msfB[parity];
        for(int i=first;i<=last;++i) ones+=_frame[i];
        return ones%2==1;
    };
    const bool yearParity=odd(17,24,54), dateParity=odd(25,35,55);
    const bool weekdayParity=odd(36,38,56), timeParity=odd(39,51,57);
    _stats.parityDate=yearParity&&dateParity&&weekdayParity;
    _stats.parityMinute=_stats.parityHour=timeParity;
    if (!yearParity || !dateParity || !weekdayParity || !timeParity) {
        ++_stats.parityErrors; return false;
    }
    auto bcd=[&](int first,int count,const int *weights) {
        int value=0,units=0;
        for(int i=0;i<count;++i) {
            value+=_frame[first+i]*weights[i];
            if(weights[i]<10) units+=_frame[first+i]*weights[i];
        }
        return units<=9 ? value : -1;
    };
    const int yearW[]={80,40,20,10,8,4,2,1}, monthW[]={10,8,4,2,1};
    const int dayW[]={20,10,8,4,2,1}, weekW[]={4,2,1};
    const int minuteW[]={40,20,10,8,4,2,1};
    int year=bcd(17,8,yearW);
    if(year<0 || year>99) return false;
    out.year=2000+year; out.month=bcd(25,5,monthW); out.day=bcd(30,6,dayW);
    int weekday=bcd(36,3,weekW);
    out.weekday=weekday==0 ? 7 : weekday;
    out.hour=bcd(39,6,dayW); out.minute=bcd(45,7,minuteW); out.second=0;
    if(out.month<1 || out.month>12 || out.day<1 ||
       out.day>daysInMonth(out.year,out.month) || weekday<0 || weekday>6 ||
       out.hour<0 || out.hour>23 || out.minute<0 || out.minute>59) return false;
    // Gregorian weekday, Sunday=0. Reject parity-correct impossible dates.
    static const int offset[]={0,3,2,5,0,3,5,1,4,6,2,4};
    int y=out.year-(out.month<3);
    if((y+y/4-y/100+y/400+offset[out.month-1]+out.day)%7!=weekday) return false;
    out.cest=_msfB[58]==1; // shared storage; MSF UI labels this BST/GMT
    out.dstChangePending=_msfB[53]==1;
    out.valid=true;
    return true;
}

void DCF77Decoder::processMsfWindow(const uint8_t bins[100],uint32_t startUs) {
    if(_mode!=SignalMode::MSF_60KHZ) return;
    if(_msfHavePrevious && (startUs-_msfPreviousUs<980000 || startUs-_msfPreviousUs>1020000)) {
        resetMsf(); _frameCount=0; _candidateValid=false; _candidateStreak=0;
        _stats.msfPhaseLocked=_stats.minuteSynced=_stats.clockLocked=false;
        ++_stats.timingErrors;
    }
    if(!_msfHavePrevious) {
        memcpy(_msfPrevious,bins,100); _msfPreviousUs=startUs; _msfHavePrevious=true; return;
    }
    uint16_t prefix[201]; prefix[0]=0;
    for(int i=0;i<200;++i) prefix[i+1]=prefix[i]+(i<100?_msfPrevious[i]:bins[i-100]);
    auto classify=[&](int phase,int &error,int &runner) {
        const int total=prefix[phase+100]-prefix[phase];
        error=runner=2000; int chosen=-1;
        for(int symbol=0;symbol<5;++symbol) {
            int length=100, overlap=prefix[phase+10]-prefix[phase];
            if(symbol==4) {length=500; overlap=prefix[phase+50]-prefix[phase];}
            else {
                if(symbol&2) {length+=100; overlap+=prefix[phase+20]-prefix[phase+10];}
                if(symbol&1) {length+=100; overlap+=prefix[phase+30]-prefix[phase+20];}
            }
            int e=total+length-2*overlap;
            if(e<error) {runner=error; error=e; chosen=symbol;}
            else if(e<runner) runner=e;
        }
        return chosen;
    };
    int best=0,bestError=2000,runner=0;
    for(int p=0;p<100;++p) {
        int e,r; classify(p,e,r);
        _msfScores[p]=(7*_msfScores[p]+e)/8;
        if(_msfScores[p]<_msfScores[best]) best=p;
    }
    classify(best,bestError,runner);
    if(!_stats.msfPhaseLocked) {
        int distance=best-_stats.msfPhaseBin;
        if(distance>50) distance-=100;
        if(distance<-50) distance+=100;
        if(bestError<=70 && runner-bestError>=30 && distance>=-3 && distance<=3) ++_msfStable;
        else _msfStable=0;
        _stats.msfPhaseBin=best;
        if(_msfStable>=4 && _msfScores[best]<100) _stats.msfPhaseLocked=true;
    }
    if(_stats.msfPhaseLocked) {
        int e,r; const int symbol=classify(_stats.msfPhaseBin,e,r);
        const bool valid=e<=70 && r-e>=30;
        const uint32_t secondUs=_msfPreviousUs+_stats.msfPhaseBin*10000UL;
        ++_stats.totalPulses; ++_stats.sampledSymbols;
        _stats.lastPulseValid=valid;
        _stats.lastPeriodUs=1000000; _stats.quality=valid ? 100-e/10 : 0;
        _stats.msfBitA=valid&&symbol<4 ? symbol>>1 : -1;
        _stats.msfBitB=valid&&symbol<4 ? symbol&1 : -1;
        _stats.lastBit=_stats.msfBitA;
        _stats.lastPulseWidthUs=valid ? (symbol==4?500000:100000+100000*((symbol>>1)+(symbol&1))) : 0;
        if(valid) {++_stats.validPulses; _msfMisses=0;} else {++_stats.invalidPulses; ++_msfMisses;}
        if(_msfMisses>=5) {
            _stats.msfPhaseLocked=_stats.minuteSynced=_stats.clockLocked=false;
            _msfCollecting=false; _msfStable=0; _frameCount=0;
            _candidateValid=false; _candidateStreak=0;
        } else if(valid && symbol==4) {
            ++_stats.minuteMarkers;
            if(_msfCollecting && _frameCount!=60) {++_stats.invalidFrames; _candidateValid=false;}
            memset(_frame,-1,sizeof(_frame)); memset(_msfB,-1,sizeof(_msfB));
            _frame[0]=0; _msfCollecting=true; _frameCount=1; _stats.minuteSynced=true;
        } else if(_msfCollecting && _frameCount<60) {
            _frame[_frameCount]=_stats.msfBitA; _msfB[_frameCount]=_stats.msfBitB; ++_frameCount;
            if(_frameCount==60) {
                memcpy(_lastFrame,_frame,60); _lastFrameCount=60;
                DCFDateTime date; const uint32_t nextMinuteUs=secondUs+1000000;
                _stats.lastFrameValid=decodeMsfFrame(date);
                if(_stats.lastFrameValid) {
                    ++_stats.validFrames; _decoded=date; _stats.lastValidFrameMs=millis();
                    DCFDateTime expected=_candidateTime; addSeconds(expected,60);
                    // Confirm radio timing and calendar advancement; reacquire across BST changes.
                    bool coherent=_candidateValid && nextMinuteUs-_msfCandidateUs>=59900000 &&
                        nextMinuteUs-_msfCandidateUs<=60100000 && sameMinute(expected,date) && expected.cest==date.cest;
                    _candidateStreak=coherent ? (_candidateStreak<255 ? _candidateStreak+1 : 255) : 1;
                    _candidateTime=date; _candidateValid=true; _msfCandidateUs=nextMinuteUs;
                    _stats.candidateMinutes=_candidateStreak; _stats.fieldConfidence=100;
                    _stats.acquisitionConfidence=_candidateStreak>=2 ? 100 : 50;
                    if(_candidateStreak>=2) {setClockBase(date,nextMinuteUs); _stats.clockLocked=true;}
                } else {
                    ++_stats.invalidFrames; _candidateValid=false; _candidateStreak=0;
                }
                _msfCollecting=false;
            }
        }
        _stats.frameBitCount=_frameCount;
        recordSampledTrace(_msfCollecting ? _frameCount-1 : 255, _stats.msfBitA, valid ? _stats.quality : 0, valid && symbol==4, false);
    }
    memcpy(_msfPrevious,bins,100); _msfPreviousUs=startUs;
}
