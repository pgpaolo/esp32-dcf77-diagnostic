"""Exercise MSF split pulses, both polarities, framing and parity rejection."""
import json
import random
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from scan_time_signals import scan_samples, decode_frame

def msf_frame(minute):
    a=[0]*60; b=[0]*60
    def put(start,weights,value):
        for i,w in enumerate(weights):
            a[start+i]=int(bool((value%10)&w) if w<10 else bool((value//10)&(w//10)))
    put(17,[80,40,20,10,8,4,2,1],26)
    put(25,[10,8,4,2,1],10)
    put(30,[20,10,8,4,2,1],2)
    put(36,[4,2,1],5)
    put(39,[20,10,8,4,2,1],12)
    put(45,[40,20,10,8,4,2,1],minute)
    a[52:60]=[0,1,1,1,1,1,1,0]; b[58]=1
    # Exercise the split 100-on / 100-off / 100-on output waveform.
    b[1]=1
    for x,y,p in ((17,24,54),(25,35,55),(36,38,56),(39,51,57)):
        b[p]=1-sum(a[x:y+1])%2
    return ['M']+list(zip(a[1:],b[1:]))

stream=bytearray([0]*370)
invalid_stream=bytearray()
def waveform(frame):
    output=bytearray()
    for symbol in frame:
        if symbol=='M': output.extend([1]*500+[0]*500)
        else:
            a,b=symbol
            output.extend([1]*100+[a]*100+[b]*100+[0]*700)
    return output

for minute in (34,35,36):
    frame=msf_frame(minute)
    assert decode_frame(frame,'MSF')['civilTime']==f'2026-10-02T12:{minute}:00'
    broken=frame.copy(); broken[54]=(broken[54][0],1-broken[54][1])
    assert decode_frame(broken,'MSF') is None
    invalid_stream.extend(waveform(broken))
    stream.extend(waveform(frame))
for invert in (False,True):
    reports=scan_samples(bytearray(v^invert for v in stream))
    selected=[r for r in reports if r['identified']]
    assert len(selected)==1 and selected[0]['protocol']=='MSF'
    assert selected[0]['activeLow']==invert
    assert len(selected[0]['validatedFrames'])==3
assert not any(r['validatedFrames'] for r in scan_samples(invalid_stream))
for constant in (0,1):
    assert not any(r['identified'] for r in scan_samples(bytearray([constant])*180000))

def fixture():
    samples=bytearray()
    for minute in range(34,42):
        bits=[0]*59; bits[17]=1; bits[20]=1
        def field(first,weights,value,parity=None):
            for offset,weight in enumerate(weights):
                bits[first+offset]=int(bool((value%10)&weight) if weight<10
                                      else bool((value//10)&(weight//10)))
            if parity is not None: bits[parity]=sum(bits[first:first+len(weights)])%2
        field(21,[1,2,4,8,10,20,40],minute,28)
        field(29,[1,2,4,8,10,20],12,35)
        field(36,[1,2,4,8,10,20],2)
        field(42,[1,2,4],5)
        field(45,[1,2,4,8,10],10)
        field(50,[1,2,4,8,10,20,40,80],26)
        bits[58]=sum(bits[36:58])%2
        for second in range(60):
            width=0 if second==59 else 200 if bits[second] else 100
            samples.extend([1]*width+[0]*(1000-width))
    packed=bytearray((len(samples)+7)//8)
    for i,value in enumerate(samples): packed[i//8]|=value<<(i%8)
    meta={"format":"DCFRAW1","sampleRateHz":1000,"encoding":"physical-high-lsb-first",
          "samples":len(samples),"activeLow":False,"timingGaps":0,"durationUs":(len(samples)-1)*1000}
    return meta,bytes(packed)

meta,packed=fixture()
dcf=bytearray((packed[i//8]>>(i%8))&1 for i in range(meta['samples']))
reports=scan_samples(dcf)
assert any(r['identified'] and r['protocol']=='DCF77' for r in reports)
assert not any(r['identified'] and r['protocol']=='MSF' for r in reports)
rng=random.Random(77)
assert not any(r['identified'] for r in scan_samples(bytearray(rng.randrange(2) for _ in range(180000))))
print('PASS: MSF/DCF identification, polarity, split pulses, parity and random-noise rejection')
