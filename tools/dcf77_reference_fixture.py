#!/usr/bin/env python3
"""
DCF77 1 kHz reference fixture.

The bit strings below are real decoded DCF77 captures published by DJ3CE from
a GNU Radio reception. In that source:
  0 = 100 ms reduced-amplitude pulse
  1 = 200 ms reduced-amplitude pulse
  2 = second 59 / missing pulse (minute marker)

Source:
https://dj3ce.darc.de/projects/dcf77/
https://dj3ce.darc.de/files/dcf77/dcf77_data.txt

This test converts the decoded capture to the same logical 1 kHz sampled form
used by the HW-364A ESP8266 path and validates minute-marker behaviour.
"""

REAL_CAPTURE = [
    "011000011010010001001010000010100010100001110101001010010012",
    "000011001010001001001110000000100010100001110101001010010012",
    "001110110000011001001001000010100010100001110101001010010012",
    "000010100001001001001101000000100010100001110101001010010012",
]

SAMPLES_PER_SECOND = 1000
BIN_MS = 10


def symbol_to_samples(symbol: str):
    samples = [0] * SAMPLES_PER_SECOND
    if symbol == "0":
        width = 100
    elif symbol == "1":
        width = 200
    elif symbol == "2":
        width = 0
    else:
        raise ValueError(symbol)

    for i in range(width):
        samples[i] = 1
    return samples


def to_10ms_bins(samples):
    assert len(samples) == 1000
    return [sum(samples[i:i + BIN_MS]) for i in range(0, 1000, BIN_MS)]


def classify_phase_aligned(bins):
    # Mirrors the firmware's two 100 ms phase-aligned halves.
    first = sum(1 for x in bins[0:10] if x > 5)
    second = sum(1 for x in bins[10:20] if x > 5)
    aligned_activity = first + second

    if aligned_activity <= 2:
        return "SYNC?"
    if first > 5 and second > 5:
        return "1"
    if first > 5 and second <= 5:
        return "0"
    return "?"


def main():
    raw_sync = 0
    confirmed_sync = 0
    pending_sync = False
    decoded = []

    stream = "".join(REAL_CAPTURE)

    # Sanity check the published frames.
    assert all(len(frame) == 61 for frame in REAL_CAPTURE)
    assert all(frame[-1] == "2" for frame in REAL_CAPTURE)

    for expected in stream:
        bins = to_10ms_bins(symbol_to_samples(expected))
        got = classify_phase_aligned(bins)
        decoded.append(got)

        if pending_sync:
            if got == "0":
                confirmed_sync += 1
            pending_sync = False

        if got == "SYNC?":
            raw_sync += 1
            pending_sync = True

        if expected == "0":
            assert got == "0"
        elif expected == "1":
            assert got == "1"
        elif expected == "2":
            assert got == "SYNC?"

    # Four real captured minute markers are present, but the final one has no
    # following frame in this fixture and therefore cannot be confirmed.
    assert raw_sync == 4, (raw_sync, decoded)
    assert confirmed_sync == 3, (confirmed_sync, decoded)

    print("DCF77 real-capture fixture: PASS")
    print(f"symbols={len(stream)} raw_sync={raw_sync} confirmed_sync={confirmed_sync}")


if __name__ == "__main__":
    main()
