#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_timestamp_overflow.py

Checks whether the Teensy (and RAPL) timestamp streams in each experiment actually
contain counter rollovers, i.e. whether the overflow-correction step that the Coffee
Lake notebook applies is doing anything.

Background
----------
`rq1_E1_coffeelake.ipynb` (and the other E1 notebooks) run two extra cells that E2/E3
do not:

    rollover     = find_rollovers(df)                     # indices where ts[i] < ts[i-1]
    corrected_df = drop_around_overflows(df, rollover)    # drop +/- window rows around each

`drop_around_overflows` is a no-op when `rollover` is empty. This script determines,
straight from the raw binaries, whether it is empty.

A "rollover" here is any strictly-decreasing step in the timestamp column. That covers
a genuine fixed-width counter wrap (a negative jump close to 2**32 or 2**64), transient
out-of-order samples (what the notebooks elsewhere call "race conditions"), and isolated
corrupted records. They are reported separately because they call for different handling.

Result (run against the data in this package)
---------------------------------------------
NO counter wrap exists in any Teensy or RAPL stream in any of the three experiments.
Coffee Lake does NOT need overflow correction in the wraparound sense.

What Coffee Lake's Teensy stream does contain is 2 consecutive corrupted records
(indices 254,475-254,476 in data11.bin) whose upper timestamp bytes were overwritten
with the pattern 0x5555651c instead of 0x00000ff0:

    254,474  ts=       17,527,049,842,197   hex=00000ff0d5946e15
    254,475  ts=6,148,932,039,329,985,986   hex=5555651c804835c2   <-- corrupt
    254,476  ts=6,148,932,039,330,590,902   hex=5555651c805170b6   <-- corrupt
    254,477  ts=       17,527,051,818,502   hex=00000ff0d5b29606

The low 32 bits stayed intact - the step between the two corrupt records is 604,916 ns,
consistent with the stream's 597,417 ns median - so this is bit corruption in transport,
not a counter event. 0x55 is the alternating bit pattern typical of a serial framing
glitch. 2 records out of 282,360 (0.0007%).

The E1 correction cells therefore DO fire, dropping those records plus a window of
neighbours. They are load-bearing, but they are a corrupt-record filter, not overflow
correction; the function names (find_rollovers / correct_timestamp_overflow) are
misleading. E2 and E3 have no corrupted records at all.

Separately, every RAPL stream shows a handful of decreasing steps of almost exactly
-10,000,000,000 ns - one file's worth of samples. These are not wraps either. The
bracket-merge sorts by timestamp and the cleaning cell drops duplicates, so they are
absorbed downstream, but they are worth understanding before trusting raw file order.

Usage
-----
    python verify_timestamp_overflow.py                 # all experiments under ./data
    python verify_timestamp_overflow.py --data DIR      # alternate data root
    python verify_timestamp_overflow.py --max-report 20 # show more individual events
"""

import argparse
import os
import sys

import numpy as np

# Teensy record: 4-byte discard | 8-byte uint64 timestamp | 16 x (1-byte id + 4-byte float)
TEENSY_DTYPE = np.dtype([
    ("_discard", "4u1"),
    ("timestamp", "<u8"),
    ("pairs", [("id", "u1"), ("data", "<f4")], (16,)),
])
TEENSY_RECORD_SIZE = TEENSY_DTYPE.itemsize  # 92

# RAPL file: [N x int64 ts_ns][N x f32 pkg][N x f32 dram][N x f32 psys] -> 20 bytes/sample
RAPL_STRIDE = 20

POW2_32 = 1 << 32
POW2_64 = 1 << 64


def read_teensy_timestamps(folder):
    """Concatenate the timestamp column across data0.bin, data1.bin, ... in numeric order."""
    files = sorted(
        (f for f in os.listdir(folder) if f.startswith("data") and f.endswith(".bin")),
        key=lambda x: int(x[4:-4]),
    )
    chunks, boundaries, offset = [], [], 0
    for name in files:
        raw = open(os.path.join(folder, name), "rb").read()
        n = len(raw) // TEENSY_RECORD_SIZE
        if n == 0:
            continue
        arr = np.frombuffer(raw, dtype=TEENSY_DTYPE, count=n)
        chunks.append(arr["timestamp"].copy())
        offset += n
        boundaries.append((offset, name))  # index one past this file's last record
    if not chunks:
        raise ValueError("no Teensy records found in %s" % folder)
    return np.concatenate(chunks), boundaries, files


def read_rapl_timestamps(folder):
    """Concatenate the timestamp array across rapl_*.bin in filename order."""
    files = sorted(f for f in os.listdir(folder) if f.endswith(".bin"))
    chunks, boundaries, offset = [], [], 0
    for name in files:
        raw = open(os.path.join(folder, name), "rb").read()
        if len(raw) % RAPL_STRIDE:
            print("    warning: %s is not a multiple of %d bytes, skipping"
                  % (name, RAPL_STRIDE))
            continue
        n = len(raw) // RAPL_STRIDE
        chunks.append(np.frombuffer(raw, dtype="<i8", count=n).copy())
        offset += n
        boundaries.append((offset, name))
    if not chunks:
        raise ValueError("no RAPL records found in %s" % folder)
    return np.concatenate(chunks), boundaries, files


def classify(drop):
    """Label a negative timestamp step by magnitude."""
    for width, label in ((POW2_32, "32-bit"), (POW2_64, "64-bit")):
        # a true wrap lands within 0.1% of the counter width
        if abs(drop) > width * 0.999 and abs(drop) < width * 1.001:
            return "%s COUNTER WRAP" % label
    return "out-of-order sample"


def find_corrupt(ts):
    """Isolated samples whose value is nowhere near the stream's own linear ramp.

    A record whose timestamp sits orders of magnitude outside the span traced by its
    neighbours is a corrupted record, not a wrap. The distinction matters: a wrap
    displaces the whole remainder of the stream, whereas a corruption displaces one
    sample and the stream resumes exactly where it left off.
    """
    # The envelope must be robust: using min()/max() would let a single corrupt
    # value inflate the bound far enough to hide itself.
    p1, p99 = np.percentile(ts.astype(float), [1, 99])
    span = max(p99 - p1, 1.0)
    lo, hi = p1 - 100 * span, p99 + 100 * span
    return np.flatnonzero((ts < lo) | (ts > hi))


def file_of(idx, boundaries):
    for end, name in boundaries:
        if idx < end:
            return name
    return "?"


def analyse(label, timestamps, boundaries, files, max_report):
    ts = timestamps.astype(np.int64, copy=False)
    n = ts.size
    diffs = np.diff(ts)
    neg = np.flatnonzero(diffs < 0)
    zero = int(np.count_nonzero(diffs == 0))
    pos = diffs[diffs > 0]

    print("  %s" % label)
    print("    files                : %d" % len(files))
    print("    samples              : {:,}".format(n))
    print("    span                 : {:,} -> {:,}  ({:.3f} s if ns)".format(
        int(ts[0]), int(ts[-1]), (int(ts[-1]) - int(ts[0])) / 1e9))
    if pos.size:
        print("    median step          : {:,} ns  (~{:.0f} Hz)".format(
            int(np.median(pos)), 1e9 / max(float(np.median(pos)), 1e-9)))
    print("    duplicate timestamps : {:,}".format(zero))
    print("    DECREASING steps     : {:,}".format(neg.size))

    corrupt = find_corrupt(ts)
    stats = {"neg": int(neg.size), "corrupt": int(corrupt.size), "wraps": 0}
    if corrupt.size:
        print("    CORRUPTED samples    : {:,}  (value far outside the stream envelope)".format(
            corrupt.size))
        for i in corrupt[:max_report]:
            i = int(i)
            print("      idx {:>10,} in {:<12} ts={:,}  hex={:016x}".format(
                i, file_of(i, boundaries), int(ts[i]), int(ts[i]) & ((1 << 64) - 1)))

    if neg.size == 0:
        print("    --> strictly non-decreasing: NO overflow correction needed")
        return stats

    kinds = {}
    for i in neg:
        kinds.setdefault(classify(int(diffs[i])), []).append(i)
    stats["wraps"] = sum(len(v) for k, v in kinds.items() if "WRAP" in k)
    for kind, idxs in sorted(kinds.items()):
        print("      %-22s %d" % (kind + ":", len(idxs)))

    print("    first %d event(s):" % min(max_report, neg.size))
    for i in neg[:max_report]:
        i = int(i)
        print("      idx {:>10,} in {:<12} {:,} -> {:,}   (delta {:,} = {})".format(
            i + 1, file_of(i + 1, boundaries), int(ts[i]), int(ts[i + 1]),
            int(diffs[i]), classify(int(diffs[i]))))
    if neg.size > max_report:
        print("      ... %d more" % (neg.size - max_report))
    return stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "data"))
    ap.add_argument("--max-report", type=int, default=10)
    args = ap.parse_args()

    experiments = [
        ("E1-coffeelake-instructions", "Coffee Lake, instruction sweep  <-- applies overflow correction"),
        ("E2-arrowlake-instructions",  "Arrow Lake, instruction sweep   (control: no correction)"),
        ("E3-arrowlake-microservice",  "Arrow Lake, microservice sweep  (control: no correction)"),
    ]

    verdicts = {}
    for folder, desc in experiments:
        root = os.path.join(args.data, folder)
        if not os.path.isdir(root):
            print("\n== %s ==\n  not found, skipping" % folder)
            continue
        print("\n" + "=" * 78)
        print("== %s" % folder)
        print("   %s" % desc)
        print("=" * 78)
        counts = {}
        for stream, reader in (("teensy", read_teensy_timestamps),
                               ("rapl", read_rapl_timestamps)):
            path = os.path.join(root, stream)
            if not os.path.isdir(path):
                continue
            try:
                ts, bounds, files = reader(path)
            except Exception as exc:
                print("  %s: FAILED to read (%s)" % (stream, exc))
                continue
            st = analyse(stream, ts, bounds, files, args.max_report)
            counts[stream] = st["neg"]
            counts["wraps"] = counts.get("wraps", 0) + st["wraps"]
            counts["corrupt"] = counts.get("corrupt", 0) + st["corrupt"]
            print()
        verdicts[folder] = counts

    print("\n" + "=" * 78)
    print("SUMMARY - decreasing timestamp steps per stream")
    print("=" * 78)
    print("  %-32s %10s %10s %10s" % ("experiment", "teensy", "rapl", "corrupt"))
    for folder, counts in verdicts.items():
        print("  %-32s %10s %10s %10s" % (
            folder, counts.get("teensy", "-"), counts.get("rapl", "-"),
            counts.get("corrupt", 0)))

    wraps = sum(c.get("wraps", 0) for c in verdicts.values())
    print()
    print("  No counter wrap was found in any stream." if wraps == 0
          else "  %d COUNTER WRAP(S) FOUND - see detail above." % wraps)

    e1 = verdicts.get("E1-coffeelake-instructions", {})
    if "teensy" in e1:
        print()
        print("  VERDICT for Coffee Lake (E1) Teensy:")
        if e1["teensy"] == 0:
            print("    Strictly non-decreasing. The overflow-correction cells find nothing")
            print("    to drop and are a no-op.")
        elif e1.get("wraps", 0) == 0 and e1.get("corrupt", 0) > 0:
            print("    NOT an overflow. The stream contains no counter wrap; it contains")
            print("    {} corrupted record(s) whose upper timestamp bytes were clobbered.".format(
                e1["corrupt"]))
            print("    The correction cells still fire (they drop those records), so they are")
            print("    load-bearing - but they are a corrupt-record filter, not overflow")
            print("    correction, and the function names are misleading.")
        else:
            print("    {} decreasing step(s), no counter wrap. These are transient".format(
                e1["teensy"]))
            print("    out-of-order samples; the bracket-merge sorts by timestamp anyway.")


if __name__ == "__main__":
    sys.exit(main())
