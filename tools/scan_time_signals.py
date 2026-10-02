"""Offline DCF77/MSF diagnostic scan of a 1 kHz DCFRAW1 recording.

MSF reference: https://www.npl.co.uk/products-services/time-frequency/
msf-radio-time-signal/msf_time_date_code
This is a strict diagnostic matcher, not a noise-recovering radio decoder.
It cannot measure the RF carrier frequency from demodulated OUT.
"""
import argparse
import datetime as dt
import json
from pathlib import Path
from compare_recording import read_recording

DCF = [(0, [(0, 100)]), (1, [(0, 200)]), ('M', [])]
MSF = [((a, b), [(0, 100)] + ([(100, 200)] if a else [])
        + ([(200, 300)] if b else [])) for a in (0, 1) for b in (0, 1)]
MSF += [('M', [(0, 500)])]

def field(bits, start, weights):
    values = bits[start:start + len(weights)]
    if any(type(v) is not int for v in values):
        raise ValueError('Missing bit')
    # Reject invalid BCD digits rather than accepting their summed value.
    if sum(v*w for v, w in zip(values, weights) if w < 10) > 9:
        raise ValueError('Invalid BCD digit')
    return sum(v*w for v, w in zip(values, weights))

def decode_frame(symbols, protocol):
    try:
        if protocol == 'DCF77':
            a = symbols
            if any(type(v) is not int for v in a[:59]): return None
            if a[0] != 0 or a[20] != 1 or a[17] + a[18] != 1: return None
            if any(sum(a[x:y+1]) % 2 for x, y in ((21,28),(29,35),(36,58))): return None
            minute = field(a,21,[1,2,4,8,10,20,40])
            hour = field(a,29,[1,2,4,8,10,20])
            day = field(a,36,[1,2,4,8,10,20])
            weekday = field(a,42,[1,2,4])
            month = field(a,45,[1,2,4,8,10])
            year = field(a,50,[1,2,4,8,10,20,40,80])
            summer = a[17]
        else:
            if any(type(v) is not tuple for v in symbols[1:60]): return None
            a = [0] + [v[0] for v in symbols[1:60]]
            b = [0] + [v[1] for v in symbols[1:60]]
            if a[52:60] != [0,1,1,1,1,1,1,0]: return None
            if any((sum(a[x:y+1])+b[p]) % 2 != 1
                   for x,y,p in ((17,24,54),(25,35,55),(36,38,56),(39,51,57))): return None
            year = field(a,17,[80,40,20,10,8,4,2,1])
            month = field(a,25,[10,8,4,2,1])
            day = field(a,30,[20,10,8,4,2,1])
            weekday = field(a,36,[4,2,1])
            hour = field(a,39,[20,10,8,4,2,1])
            minute = field(a,45,[40,20,10,8,4,2,1])
            summer = b[58]
        if not 0 <= year <= 99: return None
        civil = dt.datetime(2000+year,month,day,hour,minute)
        expected = civil.isoweekday() if protocol == 'DCF77' else (civil.weekday()+1)%7
        if weekday != expected: return None
        return {'civilTime': civil.isoformat(), 'summerTime': bool(summer)}
    except (ValueError, TypeError, IndexError):
        return None

def scan_samples(physical):
    reports = []
    for active_low in (False, True):
        prefix = [0]
        for value in physical: prefix.append(prefix[-1] + (value ^ active_low))
        for protocol, templates in (('DCF77',DCF), ('MSF',MSF)):
            best = None
            for phase in range(0,1000,10):
                symbols = []
                for start in range(phase,len(physical)-999,1000):
                    total = prefix[start+1000]-prefix[start]
                    matches = []
                    for symbol, blocks in templates:
                        expected = sum(y-x for x,y in blocks)
                        overlap = sum(prefix[start+y]-prefix[start+x] for x,y in blocks)
                        matches.append((total + expected - 2*overlap, symbol))
                    errors, symbol = min(matches,key=lambda v:v[0])
                    # At most 70/1000 mismatching samples. Adjacent templates
                    # differ by 100 ms: require a decisive best match as well.
                    runner = sorted(v[0] for v in matches)[1]
                    symbols.append(symbol if errors <= 70 and runner-errors >= 30 else None)
                frames = []
                for index, symbol in enumerate(symbols):
                    if symbol != 'M': continue
                    if protocol == 'DCF77':
                        if index < 59: continue
                        chunk = symbols[index-59:index+1]; begin = index-59
                    else:
                        if index+60 > len(symbols): continue
                        chunk = symbols[index:index+60]; begin = index
                    frame = decode_frame(chunk,protocol)
                    if frame: frames.append(dict(frame,sampleStart=phase+begin*1000))
                consecutive = 0
                for first, second in zip(frames,frames[1:]):
                    utc1 = dt.datetime.fromisoformat(first['civilTime'])-dt.timedelta(hours=first['summerTime'])
                    utc2 = dt.datetime.fromisoformat(second['civilTime'])-dt.timedelta(hours=second['summerTime'])
                    if second['sampleStart']-first['sampleStart'] == 60000 and utc2-utc1 == dt.timedelta(minutes=1):
                        consecutive += 1
                good = sum(v is not None for v in symbols)
                candidate = {'protocol':protocol,'activeLow':active_low,'phaseMs':phase,
                    'matchedSeconds':good,'totalSeconds':len(symbols),
                    'matchedPercent':round(100*good/max(1,len(symbols)),1),
                    'minuteMarkers':sum(v == 'M' for v in symbols),
                    'validatedFrames':frames,'consecutiveFramePairs':consecutive,
                    'identified':consecutive > 0}
                rank = (consecutive,len(frames),good)
                if best is None or rank > best[0]: best = rank,candidate
            reports.append(best[1])
    return reports

def scan_file(path):
    meta, normalized = read_recording(path)
    physical = bytearray((v-48)^meta['activeLow'] for v in normalized)
    return {'recording':meta,'scan':scan_samples(physical),
            'limitations':'Strict 1-second templates; no RF frequency measurement; a negative result does not prove absence of a weak/noisy station.'}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('recording',type=Path)
    parser.add_argument('--output',type=Path)
    args = parser.parse_args()
    result = json.dumps(scan_file(args.recording),indent=2)
    if args.output: args.output.write_text(result,encoding='utf-8')
    print(result)
