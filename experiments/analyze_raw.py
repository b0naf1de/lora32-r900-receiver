#!/usr/bin/env python3
"""
analyze_raw.py — measure the pulse/gap timing structure of ESP-captured RAW trains and compare
to the R900 ground truth (from r900-test-harness corpus .ook).

Ground-truth R900 (rtl_433 from RTL-SDR): base chip ~29-30us; pulses cluster 28/58/88/120us,
gaps 33/63/93/124us (1x/2x/3x/4x of ~30us).

If the ESP's base unit is ~2x (~60us), the per-edge GPIO ISR is merging chips -> R900 PCM slicer
can't sync -> CRC fail. That would mean Patch 02 (preamble) is NOT the fix; ISR speed / Option-B
envelope capture is.

Usage:
  python3 analyze_raw.py results/<label>-raw/*.txt
  python3 analyze_raw.py --raw "+50113-61+128-64+256-64..."
"""
import argparse, re, sys, glob, statistics

def parse_raw(s):
    # tokens like +1234 (pulse) or -1234 (gap)
    toks = re.findall(r"([+-])(\d+)", s)
    pulses = [int(v) for sgn, v in toks if sgn == "+"]
    gaps = [int(v) for sgn, v in toks if sgn == "-"]
    return pulses, gaps

def cluster_base(vals, maxbase=200):
    """Estimate base unit = smallest robust cluster center (ignore the huge inter-burst gaps)."""
    small = sorted(v for v in vals if 0 < v <= maxbase)
    if not small:
        return None, []
    # base = median of the lowest ~third (the 1x cluster)
    lowest = small[: max(3, len(small) // 3)]
    base = statistics.median(lowest)
    return base, small

def histo(vals, width=16, maxv=320):
    from collections import Counter
    c = Counter(min(maxv, (v // width) * width) for v in vals if 0 < v <= maxv)
    out = []
    for b in sorted(c):
        out.append(f"  {b:4d}-{b+width-1:<4d}us: {'#'*min(40,c[b])} ({c[b]})")
    return "\n".join(out)

def report(label, pulses, gaps):
    pb, ps = cluster_base(pulses)
    gb, gs = cluster_base(gaps)
    print(f"\n===== {label} =====")
    print(f"pulses n={len(pulses)}  gaps n={len(gaps)}")
    print(f"PULSE base ~{pb}us   GAP base ~{gb}us")
    R900 = 29.5
    if pb:
        ratio = pb / R900
        verdict = ("~1x: matches R900 chip (GOOD)" if 0.7 <= ratio <= 1.4 else
                   f"~{ratio:.1f}x R900 chip -> CHIPS MERGED (ISR too slow / wrong rate)")
        print(f"vs R900 chip {R900}us: pulse base is {verdict}")
    print("PULSE histogram (us):")
    print(histo(pulses))
    print("GAP histogram (us):")
    print(histo(gaps))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="*")
    ap.add_argument("--raw")
    args = ap.parse_args()
    allp, allg = [], []
    if args.raw:
        p, g = parse_raw(args.raw)
        report("--raw", p, g); return
    files = []
    for f in args.files:
        files.extend(glob.glob(f))
    if not files:
        print("no RAW files found"); sys.exit(1)
    for f in files:
        txt = open(f).read()
        fp, fg = [], []
        for line in txt.splitlines():
            if line.startswith("#"): continue
            p, g = parse_raw(line)
            fp += p; fg += g
        report(f, fp, fg)
        allp += fp; allg += fg
    if len(files) > 1:
        report("ALL COMBINED", allp, allg)

if __name__ == "__main__":
    main()
