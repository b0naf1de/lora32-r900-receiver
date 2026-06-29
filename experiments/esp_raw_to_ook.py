#!/usr/bin/env python3
"""
esp_raw_to_ook.py — convert ESP-captured RAW pulse trains into rtl_433 .ook files and (optionally)
decode them with desktop rtl_433.

This isolates capture quality from the ESP's embedded slicer:
  - if desktop rtl_433 decodes the ESP pulses but the ESP firmware didn't -> embedded slicer /
    preamble loss is the remaining blocker (Patch 02 territory)
  - if desktop rtl_433 ALSO fails -> the captured pulse train itself is still wrong (width/dropout)

ESP RAW token stream is "+<pulse_us>-<gap_us>..." (one train per RAW line).

Usage:
  python3 esp_raw_to_ook.py results/<label>-raw/*.txt --decode
  python3 esp_raw_to_ook.py --raw "+128-64+256-64..." --decode
"""
import argparse, glob, os, re, subprocess, sys, tempfile

OOK_HEADER = """;pulse data
;version 1
;timescale 1us
;ook {n} pulses
;samplerate 2400000 Hz
"""

def parse_trains(text):
    """Yield lists of (pulse,gap) per RAW line."""
    for line in text.splitlines():
        if "RAW" in line:
            line = line.split(":", 1)[-1]
        toks = re.findall(r"([+-])(\d+)", line)
        if not toks:
            continue
        pulses, gaps, cur = [], [], None
        # build (pulse,gap) pairs: + sets pulse, following - sets gap
        train = []
        p = None
        for sgn, v in toks:
            v = int(v)
            if sgn == "+":
                p = v
            else:
                if p is not None:
                    train.append((p, v))
                    p = None
        if train:
            yield train

def write_ook(train, path):
    # clamp absurd leading/trailing values; rtl_433 tolerates but keep sane
    with open(path, "w") as f:
        f.write(OOK_HEADER.format(n=len(train)))
        for p, g in train:
            f.write(f"{max(1,p)} {max(1,g)}\n")

def decode(path):
    try:
        out = subprocess.run(["rtl_433", "-r", path], capture_output=True, text=True, timeout=30)
        txt = out.stdout + out.stderr
    except Exception as e:
        return f"(rtl_433 error: {e})"
    hits = [l for l in txt.splitlines() if re.search(r"model|consumption|Neptune|R900|id ", l)]
    return "\n".join(hits) if hits else "(no decode)"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="*")
    ap.add_argument("--raw")
    ap.add_argument("--decode", action="store_true")
    ap.add_argument("--outdir", default=None)
    args = ap.parse_args()

    texts = []
    if args.raw:
        texts.append(("inline", args.raw))
    for pat in args.files:
        for f in glob.glob(pat):
            texts.append((f, open(f).read()))
    if not texts:
        print("no input"); sys.exit(1)

    outdir = args.outdir or tempfile.mkdtemp(prefix="esp_ook_")
    os.makedirs(outdir, exist_ok=True)
    total = decoded = 0
    for name, text in texts:
        for i, train in enumerate(parse_trains(text)):
            total += 1
            base = os.path.splitext(os.path.basename(name))[0]
            ook = os.path.join(outdir, f"{base}_{i}.ook")
            write_ook(train, ook)
            if args.decode:
                res = decode(ook)
                ok = "Neptune" in res or "R900" in res or "consumption" in res
                if ok: decoded += 1
                print(f"[{base}#{i} npulses={len(train)}] {'DECODE!' if ok else 'no'}: {res.splitlines()[0] if res!='(no decode)' else 'no decode'}")
                if ok:
                    for l in res.splitlines(): print("    "+l)
    print(f"\n{decoded}/{total} trains decoded by desktop rtl_433. .ook files in {outdir}")

if __name__ == "__main__":
    main()
