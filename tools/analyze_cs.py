#!/usr/bin/env python3
"""
analyze_cs.py - Statistics for the context-switch samples dumped by `make bench`.

Usage:
    python3 tools/analyze_cs.py build/cs_samples.bin [--cpu-hz 168000000] [--csv out.csv]

The file is the raw g_cs_samples[] array: little-endian uint16, one value per
switch, in CPU cycles, calibration already subtracted. No third-party modules.
"""
import argparse
import struct
import sys


def percentile(sorted_vals, p):
    if not sorted_vals:
        return 0
    k = (len(sorted_vals) - 1) * p / 100.0
    lo, hi = int(k), min(int(k) + 1, len(sorted_vals) - 1)
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * (k - lo)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--cpu-hz", type=float, default=168e6)
    ap.add_argument("--csv", help="also write the samples as CSV")
    a = ap.parse_args()

    data = open(a.file, "rb").read()
    n = len(data) // 2
    if n == 0:
        sys.exit("no samples in file")
    s = list(struct.unpack("<%dH" % n, data[: n * 2]))
    ss = sorted(s)
    mean = sum(s) / n
    var = sum((x - mean) ** 2 for x in s) / n
    ns = 1e9 / a.cpu_hz

    print("\ncontext-switch latency, %d samples @ %.0f MHz" % (n, a.cpu_hz / 1e6))
    print("  mean   (XX) : %8.2f cycles  %8.1f ns" % (mean, mean * ns))
    print("  max    (YY) : %8d cycles  %8.1f ns   (sample #%d)" % (ss[-1], ss[-1] * ns, s.index(ss[-1])))
    print("  min         : %8d cycles" % ss[0])
    print("  median      : %8.1f cycles" % percentile(ss, 50))
    print("  p99         : %8.1f cycles" % percentile(ss, 99))
    print("  p99.9       : %8.1f cycles" % percentile(ss, 99.9))
    print("  std dev     : %8.2f cycles" % var ** 0.5)

    # histogram of distinct values (switch times cluster on a few values)
    counts = {}
    for x in s:
        counts[x] = counts.get(x, 0) + 1
    print("\n  cycles  count")
    top = max(counts.values())
    for v in sorted(counts):
        bar = "#" * max(1, int(40 * counts[v] / top))
        print("  %6d  %6d  %s" % (v, counts[v], bar))
        if len(counts) > 40 and v > percentile(ss, 99.9):
            print("  ... (%d more distinct values)" % (len(counts) - list(sorted(counts)).index(v) - 1))
            break

    if a.csv:
        with open(a.csv, "w") as f:
            f.write("index,cycles\n")
            for i, x in enumerate(s):
                f.write("%d,%d\n" % (i, x))
        print("\n  samples written to", a.csv)

    print("\nResume line: measuring %.0f cycles average and %d cycles worst-case over %d switches"
          % (mean, ss[-1], n))


if __name__ == "__main__":
    main()
