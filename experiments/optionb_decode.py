#!/usr/bin/env python3
"""
optionb_decode.py — software-slice the SX1276 RSSI-envelope captures from the Option-B firmware.

Input: lines from main_optionb.cpp serial:
  BURST dt=3.930us/sample N=8000 min=146(-73.0dBm) samples_in_burst=1065
  CAP:<hex bytes>             (each byte = RegRssiValue; dBm = -val/2; LOWER val = STRONGER signal)

We threshold the magnitude (signal "on" = val below threshold), turn runs into pulse/gap durations
(x dt), histogram them (looking for the clean 1x/2x/3x R900 structure the hardware slicer destroyed),
write .ook, and decode with desktop rtl_433.

Usage: python3 optionb_decode.py /tmp/optionb-caps.txt
"""
FRAC=0.55
import re, sys, subprocess, tempfile, os, statistics

def parse(path):
    bursts = []
    dt = 3.93
    for line in open(path):
        m = re.search(r"dt=([\d.]+)us", line)
        if m: dt = float(m.group(1))
        if line.startswith("CAP:"):
            hexs = line.strip()[4:]
            vals = [int(hexs[i:i+2], 16) for i in range(0, len(hexs)-1, 2)]
            bursts.append((dt, vals))
    return bursts

def median3(v):
    out = v[:]
    for i in range(1, len(v) - 1):
        a, b, c = v[i-1], v[i], v[i+1]
        out[i] = a + b + c - max(a, b, c) - min(a, b, c)  # median of 3
    return out

def otsu(vals):
    """Otsu's method: threshold (val) that best separates the on/off bimodal magnitude histogram."""
    hist = [0] * 256
    for v in vals: hist[v] += 1
    total = len(vals)
    sumAll = sum(i * hist[i] for i in range(256))
    wB = sumB = 0; best = 0; bestT = 128
    for t in range(256):
        wB += hist[t]
        if wB == 0: continue
        wF = total - wB
        if wF == 0: break
        sumB += t * hist[t]
        mB = sumB / wB
        mF = (sumAll - sumB) / wF
        between = wB * wF * (mB - mF) ** 2
        if between > best: best = between; bestT = t
    return bestT

def slice_runs(vals, dt, thresh):
    """Return list of (level, duration_us) runs. level 1 = signal on (val<thresh)."""
    runs = []
    cur = 1 if vals[0] < thresh else 0
    n = 0
    for v in vals:
        lv = 1 if v < thresh else 0
        if lv == cur:
            n += 1
        else:
            runs.append((cur, n * dt)); cur = lv; n = 1
    runs.append((cur, n * dt))
    return runs

def deglitch(runs, min_us=14):
    """Merge runs shorter than min_us into their neighbours (sub-chip fragments are noise)."""
    changed = True
    runs = runs[:]
    while changed and len(runs) > 2:
        changed = False
        for i in range(len(runs)):
            if runs[i][1] < min_us:
                # flip this short run's level by merging with neighbours of the opposite level
                lvl, dur = runs[i]
                left = runs[i-1] if i > 0 else None
                right = runs[i+1] if i < len(runs)-1 else None
                merged = dur + (left[1] if left else 0) + (right[1] if right else 0)
                newlvl = left[0] if left else (right[0] if right else lvl)
                lo = i-1 if left else i
                hi = i+1 if right else i
                runs = runs[:lo] + [(newlvl, merged)] + runs[hi+1:]
                changed = True
                break
    return runs

def to_ook(runs, path):
    # R900 burst: find the contiguous active region, emit pulse(on)/gap(off) pairs
    with open(path, "w") as f:
        f.write(";pulse data\n;version 1\n;timescale 1us\n;samplerate 2400000 Hz\n")
        i = 0
        # pair up on->off
        pend = None
        for level, dur in runs:
            d = max(1, int(round(dur)))
            if level == 1:
                pend = d
            else:
                if pend is not None:
                    f.write(f"{pend} {d}\n"); pend = None
        if pend is not None:
            f.write(f"{pend} 100\n")

def decode(path):
    try:
        out = subprocess.run(["rtl_433", "-r", path], capture_output=True, text=True, timeout=30)
        txt = out.stdout + out.stderr
    except Exception as e:
        return f"(err {e})"
    hits = [l for l in txt.splitlines() if re.search(r"Neptune|R900|consumption|^model", l)]
    return hits

def histo(durs, lab):
    from collections import Counter
    c = Counter(int(d//4)*4 for d in durs if d <= 140)
    print(f"  {lab} (4us bins):")
    for b in sorted(c):
        print(f"    {b:3d}: {'#'*min(40,c[b])} ({c[b]})")

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/optionb-caps.txt"
    bursts = parse(path)
    print(f"{len(bursts)} bursts")
    outdir = tempfile.mkdtemp(prefix="optionb_")
    decoded = 0
    for bi, (dt, vals) in enumerate(bursts):
        vals = median3(vals)              # kill single-sample RSSI glitches
        mn = min(vals)
        sv = sorted(vals)
        noise = sv[int(len(sv) * 0.90)]   # "off"/noise level (gaps return toward noise)
        ot = otsu(vals)
        thresh = int(min(ot, mn + FRAC * (noise - mn)))  # balanced toward signal to recover gaps
        runs = deglitch(slice_runs(vals, dt, thresh), min_us=14)  # drop sub-chip fragments
        ons = [d for lv, d in runs if lv == 1]
        if not ons:
            continue
        ook = os.path.join(outdir, f"burst{bi}.ook")
        to_ook(runs, ook)
        res = decode(ook)
        ok = any("Neptune" in l or "R900" in l or "consumption" in l for l in res)
        print(f"\n--- burst {bi}: dt={dt} min_val={mn}({-mn/2:.0f}dBm) thresh={thresh} "
              f"on-pulses={len(ons)} {'*** DECODE ***' if ok else ''}")
        if bi < 3 or ok:
            histo(ons, "ON-pulse widths")
        if ok:
            decoded += 1
            for l in res: print("   ", l)
    print(f"\n{decoded}/{len(bursts)} bursts decoded as R900. .ook in {outdir}")

if __name__ == "__main__":
    main()
