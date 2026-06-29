#!/usr/bin/env python3
"""
trial.py — repeatable bring-up experiment runner for the LoRa32/SX1276 R900 receiver.

Captures ESP serial (the SX1276 firmware under test) for a fixed window, optionally runs the
laptop RTL-SDR as a parallel ground-truth oracle parked on the same band, then computes a
comparable summary and appends a row to experiments/results/RESULTS.md.

It does NOT flash — flash the variant first (pio run -t upload), then run this.

Usage:
  python3 trial.py --label thr64 --secs 90                       # ESP-only (noise characterization)
  python3 trial.py --label A-pending --secs 900 --oracle \
        --center 915.662 --bw 0.25                                # ESP + oracle decode hunt

Metrics captured per trial:
  ESP:   captured trains, ignored, train-rate/min, Signal-RSSI min/median/max, #undecoded,
         #REAL decodes (model != status/undecoded), strongest burst, preamble sightings.
  Oracle: total meter hits, in-band hits (|freq-center| <= bw/2) w/ time/freq/rssi/consumption.
  Coincidence: for each in-band oracle hit, the max ESP Signal-RSSI within +/-3s and whether
               the ESP decoded anything in that window. (the money-shot test)
  RAW pulse trains within +/-3s of in-band oracle hits are saved to results/<label>-raw/.
"""
import argparse, json, os, re, statistics, subprocess, sys, time, signal
from datetime import datetime

ESP_PORT = os.environ.get("ESP_PORT", "/dev/ttyUSB0")  # set to your board (pio device list), or export ESP_PORT
ESP_BAUD = 115200
METER_ID = os.environ.get("METER_ID", "")  # your Neptune R900 meter id (blank = match any R900)
HERE = os.path.dirname(os.path.abspath(__file__))
RESULTS_DIR = os.path.join(HERE, "results")
ANY_R900 = False  # if True, treat ANY Neptune-R900 (any id) as a coincidence target (faster samples)

# ---- regexes over ESP serial -------------------------------------------------
RE_SIGNAL = re.compile(r"Signal length: (\d+),.*Signal RSSI: (-?\d+).*pulses: (\d+)")
RE_IGNORED = re.compile(r"Ignored Signal length: (\d+)")
RE_RAW = re.compile(r"RAW \((\d+)\): (.+)")
RE_MSG = re.compile(r"Received message : (\{.*\})")
RE_PREAMBLE = re.compile(r"a{6,}b", re.I)  # aaaaaaab-style clean preamble in any sliced dump

def now_s(): return time.time()

def read_esp(port, baud, secs, raw_sink):
    """Read ESP serial for `secs`. Returns list of (t, line). Also collects RAW lines via raw_sink."""
    import serial
    s = serial.Serial(port, baud, timeout=1)
    out = []
    t0 = now_s()
    while now_s() - t0 < secs:
        try:
            line = s.readline().decode("utf-8", "replace").rstrip("\n")
        except Exception:
            continue
        if line:
            out.append((now_s(), line))
    s.close()
    return out

def start_oracle(center, logpath):
    """Launch rtl_433 parked on center MHz, JSON to logpath. Returns Popen."""
    f = open(logpath, "w")
    p = subprocess.Popen(
        ["rtl_433", "-f", f"{center}M", "-s", "2400000",
         "-M", "level", "-M", "time:iso", "-F", "json"],
        stdout=f, stderr=subprocess.DEVNULL)
    return p, f

def parse_oracle(logpath):
    hits = []
    try:
        for ln in open(logpath):
            try:
                j = json.loads(ln)
            except Exception:
                continue
            model = j.get("model", "")
            is_r900 = "R900" in model or "Neptune" in model
            if ANY_R900:
                if not is_r900:
                    continue
            else:
                if str(j.get("id")) != METER_ID:
                    continue
            hits.append({
                "time": j.get("time", ""),
                "freq": float(j.get("freq", 0)),
                "rssi": float(j.get("rssi", 0)),
                "id": j.get("id"),
                "consumption": j.get("consumption"),
                "leak": j.get("leak"),
                "epoch": _iso_to_epoch(j.get("time", "")),
            })
    except FileNotFoundError:
        pass
    return hits

def _iso_to_epoch(t):
    # rtl_433 emits local time "2026-06-28T22:44:25"
    try:
        return datetime.strptime(t, "%Y-%m-%dT%H:%M:%S").timestamp()
    except Exception:
        return 0.0

def analyze(esp_lines, oracle_hits, center, bw, label):
    rssis, pulses_list = [], []
    captured = ignored = undecoded = real = 0
    decodes = []
    r900_decodes = []
    preamble_sightings = 0
    raw_events = []  # (t, dur, raw)
    for t, ln in esp_lines:
        m = RE_SIGNAL.search(ln)
        if m:
            captured += 1
            rssis.append(int(m.group(2)))
            pulses_list.append(int(m.group(3)))
            continue
        if RE_IGNORED.search(ln):
            ignored += 1
            continue
        mr = RE_RAW.search(ln)
        if mr:
            raw_events.append((t, int(mr.group(1)), mr.group(2)))
            if RE_PREAMBLE.search(ln):
                preamble_sightings += 1
            continue
        mm = RE_MSG.search(ln)
        if mm:
            try:
                j = json.loads(mm.group(1))
            except Exception:
                continue
            model = j.get("model", "")
            if model in ("status", "undecoded signal"):
                if model == "undecoded signal":
                    undecoded += 1
            else:
                real += 1
                decodes.append(j)
                if "R900" in model or "Neptune" in model or str(j.get("id")) == METER_ID:
                    r900_decodes.append(j)
            continue
        if RE_PREAMBLE.search(ln):
            preamble_sightings += 1

    half = bw / 2.0
    inband = [h for h in oracle_hits if abs(h["freq"] - center) <= half]

    # coincidence: for each in-band oracle hit, max ESP RSSI within +/-3s, decode?
    coincidences = []
    for h in inband:
        he = h["epoch"]
        window = [(t, ln) for (t, ln) in esp_lines if he and abs(t - he) <= 3.0]
        wr = []
        wdec = 0
        for t, ln in window:
            mm = RE_SIGNAL.search(ln)
            if mm: wr.append(int(mm.group(2)))
            md = RE_MSG.search(ln)
            if md:
                try:
                    jj = json.loads(md.group(1))
                    if jj.get("model") not in ("status", "undecoded signal"):
                        wdec += 1
                except Exception:
                    pass
        coincidences.append({
            "oracle_time": h["time"], "freq": h["freq"], "oracle_rssi": h["rssi"],
            "esp_max_rssi": max(wr) if wr else None,
            "esp_bursts_in_window": len(wr),
            "esp_decoded": wdec,
        })

    summary = {
        "label": label,
        "esp_captured_trains": captured,
        "esp_ignored": ignored,
        "esp_undecoded": undecoded,
        "esp_real_decodes": real,
        "esp_r900_decodes": len(r900_decodes),
        "r900_decodes": r900_decodes,
        "other_decode_models": sorted({d.get("model","") for d in decodes if d not in r900_decodes}),
        "esp_rssi_min": min(rssis) if rssis else None,
        "esp_rssi_med": int(statistics.median(rssis)) if rssis else None,
        "esp_rssi_max": max(rssis) if rssis else None,
        "esp_pulses_med": int(statistics.median(pulses_list)) if pulses_list else None,
        "preamble_sightings": preamble_sightings,
        "oracle_total_hits": len(oracle_hits),
        "oracle_inband_hits": len(inband),
        "decodes": decodes,
        "coincidences": coincidences,
        "inband": inband,
        "raw_events": raw_events,
    }
    return summary

def save_raw_near_inband(summary, label):
    if not summary["inband"]:
        return None
    d = os.path.join(RESULTS_DIR, f"{label}-raw")
    os.makedirs(d, exist_ok=True)
    saved = 0
    for h in summary["inband"]:
        he = h["epoch"]
        if not he: continue
        near = [(t, dur, raw) for (t, dur, raw) in summary["raw_events"] if abs(t - he) <= 3.0]
        if not near: continue
        fn = os.path.join(d, f"oracle_{h['time'].replace(':','')}_{h['freq']}.txt")
        with open(fn, "w") as o:
            o.write(f"# oracle in-band hit {h['time']} freq={h['freq']} rssi={h['rssi']} consumption={h['consumption']}\n")
            for t, dur, raw in near:
                o.write(f"RAW({dur}): {raw}\n")
        saved += 1
    return saved

def write_results(summary, args):
    os.makedirs(RESULTS_DIR, exist_ok=True)
    # full JSON
    jpath = os.path.join(RESULTS_DIR, f"{summary['label']}.json")
    dump = {k: v for k, v in summary.items() if k not in ("raw_events",)}
    json.dump(dump, open(jpath, "w"), indent=2, default=str)
    # markdown row
    mins = args.secs / 60.0
    rate = summary["esp_captured_trains"] / mins if mins else 0
    table = os.path.join(RESULTS_DIR, "RESULTS.md")
    new = not os.path.exists(table)
    with open(table, "a") as f:
        if new:
            f.write("# R900 SX1276 trial results\n\n")
            f.write("| label | secs | trains | trains/min | ignored | RSSI min/med/max | undecoded | **R900 dec** | other decodes | preamble | oracle hits (inband) | best coincidence |\n")
            f.write("|---|---|---|---|---|---|---|---|---|---|---|---|\n")
        best_co = ""
        if summary["coincidences"]:
            c = max(summary["coincidences"], key=lambda x: (x["esp_decoded"], x["esp_max_rssi"] or -999))
            best_co = f"f{c['freq']} oRSSI{c['oracle_rssi']:.0f} espRSSI{c['esp_max_rssi']} dec{c['esp_decoded']}"
        others = ",".join(summary.get("other_decode_models", [])) or "-"
        f.write(f"| {summary['label']} | {int(args.secs)} | {summary['esp_captured_trains']} | "
                f"{rate:.1f} | {summary['esp_ignored']} | "
                f"{summary['esp_rssi_min']}/{summary['esp_rssi_med']}/{summary['esp_rssi_max']} | "
                f"{summary['esp_undecoded']} | **{summary['esp_r900_decodes']}** | {others} | "
                f"{summary['preamble_sightings']} | {summary['oracle_total_hits']}({summary['oracle_inband_hits']}) | "
                f"{best_co} |\n")
    return jpath

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", required=True)
    ap.add_argument("--secs", type=float, default=90)
    ap.add_argument("--oracle", action="store_true")
    ap.add_argument("--center", type=float, default=915.662)
    ap.add_argument("--bw", type=float, default=0.25, help="ESP RX bandwidth MHz (for in-band calc)")
    ap.add_argument("--any-r900", action="store_true", help="capture/coincide on ANY R900 meter, not just ours")
    args = ap.parse_args()
    global ANY_R900
    ANY_R900 = args.any_r900

    os.makedirs(RESULTS_DIR, exist_ok=True)
    orpath = os.path.join(RESULTS_DIR, f"{args.label}-oracle.log")
    proc = ofile = None
    if args.oracle:
        proc, ofile = start_oracle(args.center, orpath)
        time.sleep(2)

    print(f"[trial {args.label}] capturing ESP for {args.secs:.0f}s "
          f"(oracle={'on @'+str(args.center) if args.oracle else 'off'})", flush=True)
    esp_lines = read_esp(ESP_PORT, ESP_BAUD, args.secs, None)

    if proc:
        proc.send_signal(signal.SIGINT)
        time.sleep(1)
        proc.kill()
        ofile.close()

    oracle_hits = parse_oracle(orpath) if args.oracle else []
    summary = analyze(esp_lines, oracle_hits, args.center, args.bw, args.label)
    saved = save_raw_near_inband(summary, args.label)
    jpath = write_results(summary, args)

    print(f"[trial {args.label}] DONE: trains={summary['esp_captured_trains']} "
          f"ignored={summary['esp_ignored']} undecoded={summary['esp_undecoded']} "
          f"R900_DECODES={summary['esp_r900_decodes']} other_decodes={summary['esp_real_decodes']-summary['esp_r900_decodes']}{summary['other_decode_models']} "
          f"RSSI(min/med/max)={summary['esp_rssi_min']}/{summary['esp_rssi_med']}/{summary['esp_rssi_max']} "
          f"oracle_hits={summary['oracle_total_hits']} inband={summary['oracle_inband_hits']}", flush=True)
    if summary["r900_decodes"]:
        print("  *** R900 DECODES ***")
        for d in summary["r900_decodes"][:5]:
            print("   ", json.dumps(d), flush=True)
    for c in summary["coincidences"]:
        print(f"  coincidence: oracle {c['oracle_time']} f={c['freq']} "
              f"oRSSI={c['oracle_rssi']:.0f} -> ESP maxRSSI={c['esp_max_rssi']} "
              f"bursts={c['esp_bursts_in_window']} decoded={c['esp_decoded']}", flush=True)
    if saved:
        print(f"  saved {saved} RAW capture(s) near in-band hits -> results/{args.label}-raw/", flush=True)
    print(f"  full: {jpath}", flush=True)

if __name__ == "__main__":
    main()
