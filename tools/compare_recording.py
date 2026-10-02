"""Replay DCFRAW1 in production decoder and optional pinned Udo reference."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

UDO_COMMIT = "00d2450f47311268ba5de17f35480781928b34dc"
ROOT = Path(__file__).resolve().parents[1]

def read_recording(path):
    with Path(path).open("rb") as stream:
        line = stream.readline(4097)
        if len(line)>4096 or not line.endswith(b"\n"):
            raise ValueError("Invalid or oversized recording header")
        meta = json.loads(line)
        count = meta.get("samples")
        if (meta.get("format")!="DCFRAW1" or meta.get("sampleRateHz")!=1000
            or meta.get("encoding")!="physical-high-lsb-first"
            or type(count) is not int or not 0<count<=600000
            or type(meta.get("activeLow")) is not bool):
            raise ValueError("Unsupported recording format")
        packed = stream.read((count+7)//8 + 1)
        if len(packed)!=(count+7)//8:
            raise ValueError("Truncated recording or unexpected trailing bytes")
    # Replays require a uniform timeline; do not silently discard measured gaps.
    expected = (count-1)*1000
    if meta.get("timingGaps",0) or abs(meta.get("durationUs",0)-expected)>expected*0.01:
        raise ValueError("Non-uniform sampling: timing gaps/drift require timestamp-aware analysis")
    samples = bytes(48 + (((packed[i//8]>>(i%8))&1)^meta["activeLow"])
                    for i in range(count))
    return meta,samples

def compare(path, udo_dir=None, compiler="g++"):
    meta,samples=read_recording(path)
    if not shutil.which(compiler):
        raise RuntimeError("A host C++ compiler (g++) is required; use Linux/GitHub Actions")
    with tempfile.TemporaryDirectory(prefix="dcf-replay-") as scratch:
        scratch=Path(scratch); data=scratch/"samples.txt"; data.write_bytes(samples)
        project=scratch/"project"
        subprocess.run([compiler,"-std=c++17","-O2","-DESP8266",
            "-I"+str(ROOT/"tools/tests"),"-I"+str(ROOT/"include"),
            str(ROOT/"tools/replay_dcf77.cpp"),str(ROOT/"src/dcf77_decoder.cpp"),
            str(ROOT/"src/raw_recording.cpp"),"-o",str(project)],check=True)
        results=[json.loads(subprocess.check_output([str(project),str(data)]))]
        if udo_dir:
            udo_dir=Path(udo_dir).resolve()
            revision=subprocess.check_output(["git","-c","safe.directory="+udo_dir.as_posix(),
                "-C",str(udo_dir),"rev-parse","HEAD"],text=True).strip()
            if revision!=UDO_COMMIT: raise ValueError("Udo reference revision does not match pinned commit")
            reference=scratch/"udo"
            subprocess.run([compiler,"-std=c++17","-O2","-D__unit_test__",
                "-I"+str(udo_dir),"-include",str(ROOT/"tools/reference/arduino_host.h"),
                str(ROOT/"tools/reference/replay_udo.cpp"),"-o",str(reference)],check=True)
            results.append(json.loads(subprocess.check_output([str(reference),str(data)])))
    return {"recording":meta,"results":results,"referenceCommit":UDO_COMMIT if udo_dir else None}

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recording",type=Path)
    parser.add_argument("--udo-dir",type=Path)
    args=parser.parse_args()
    print(json.dumps(compare(args.recording,args.udo_dir),indent=2))
