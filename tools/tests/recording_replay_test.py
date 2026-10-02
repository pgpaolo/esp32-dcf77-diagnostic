import json
from pathlib import Path
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from compare_recording import read_recording, compare

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

with tempfile.TemporaryDirectory() as scratch:
    path=Path(scratch)/"clean.dcfraw"; meta,packed=fixture()
    def write(data=packed): path.write_bytes(json.dumps(meta).encode()+b"\n"+data)
    write(); assert len(read_recording(path)[1])==480000
    for bad in (packed[:-1],packed+b"\0"):
        write(bad)
        try: read_recording(path)
        except ValueError: pass
        else: raise AssertionError("Bad capture length accepted")
    meta["timingGaps"]=1; write()
    try: read_recording(path)
    except ValueError: pass
    else: raise AssertionError("Timing gaps silently ignored")
    meta["timingGaps"]=0; write()
    report=compare(path,sys.argv[1]); print(json.dumps(report))
    assert report["results"][0]["validFrames"]>=2
    assert report["results"][1]["syncedTicks"]>0
print("PASS: recording format and both production decoders on clean 8-minute stream")
