#!/usr/bin/env python3
"""
optionb_bitclock.py — decode Option-B RSSI captures via BIT-CLOCK RECOVERY (not pulse-slicing).

The RSSI envelope preserves R900's on/off structure but its fast-attack/slow-decay asymmetry
corrupts carrier-OFF gap durations, defeating rtl_433's PCM pulse-slicer. Bit-clock recovery
sidesteps that: sample the envelope once per 30.52us bit, get the bit value (on/off), and
re-synthesize a CLEAN .ook (exact 30us grid) from the recovered bits -> rtl_433 frames + decodes.

We brute-force (threshold, samples-per-bit, phase) and let rtl_433 be the success oracle.

Usage: python3 optionb_bitclock.py /tmp/optionb-caps3.txt
"""
import re, sys, subprocess, tempfile, os, statistics

BIT_US = 30.5176  # 1/32768 s

def parse(path):
    out, dt = [], 3.93
    for line in open(path):
        m = re.search(r"dt=([\d.]+)us", line)
        if m: dt = float(m.group(1))
        if line.startswith("CAP:"):
            h = line.strip()[4:]
            out.append((dt, [int(h[i:i+2],16) for i in range(0,len(h)-1,2)]))
    return out

def median3(v):
    o=v[:]
    for i in range(1,len(v)-1):
        a,b,c=v[i-1],v[i],v[i+1]; o[i]=a+b+c-max(a,b,c)-min(a,b,c)
    return o

def bits_to_ook(bits, path):
    """Run-length the bitstream into clean 30us pulse(1-run)/gap(0-run) pairs."""
    runs=[]; cur=bits[0]; n=0
    for b in bits:
        if b==cur: n+=1
        else: runs.append((cur,n)); cur=b; n=1
    runs.append((cur,n))
    with open(path,"w") as f:
        f.write(";pulse data\n;version 1\n;timescale 1us\n;samplerate 2400000 Hz\n")
        pend=None
        for lv,k in runs:
            us=int(round(k*BIT_US))
            if lv==1: pend=us
            else:
                if pend is not None: f.write(f"{pend} {us}\n"); pend=None
        if pend is not None: f.write(f"{pend} 100\n")

def decode(path):
    try:
        r=subprocess.run(["rtl_433","-r",path],capture_output=True,text=True,timeout=20)
        t=r.stdout+r.stderr
    except Exception as e: return None
    if "Neptune" in t or "R900" in t or "consumption" in t:
        return [l for l in t.splitlines() if re.search(r"Neptune|R900|consumption|^model|id ",l)]
    return None

def recover(vals, dt, thresh, spb, phase):
    """Majority-vote each bit over the central portion of its window (robust to edge bleed)."""
    nbits=int((len(vals)-phase)/spb)
    bits=[]
    guard=spb*0.25  # ignore the outer 25% near transitions
    for k in range(nbits):
        a=phase+k*spb+guard; b=phase+(k+1)*spb-guard
        ia, ib = int(a), int(b)
        if ib>=len(vals): break
        on=sum(1 for i in range(ia, ib+1) if vals[i]<thresh)
        bits.append(1 if on*2 >= (ib-ia+1) else 0)
    return bits

def main():
    path=sys.argv[1] if len(sys.argv)>1 else "/tmp/optionb-caps3.txt"
    bursts=parse(path)
    print(f"{len(bursts)} bursts")
    outdir=tempfile.mkdtemp(prefix="optionb_bc_")
    for bi,(dt,vals) in enumerate(bursts):
        vals=median3(vals)
        mn=min(vals); sv=sorted(vals); noise=sv[int(len(sv)*0.9)]
        # RSSI modulation is SHALLOW (slow decay): the "off" level inside the burst is far below
        # the global noise floor. Find the burst-active window and use ITS local on/off levels.
        glob_mid=(mn+noise)//2
        active=[i for i,x in enumerate(vals) if x<glob_mid]
        if active:
            lo,hi=active[0],active[-1]
            region=vals[lo:hi+1]
            rmn=min(region); rsv=sorted(region); roff=rsv[int(len(rsv)*0.75)]  # local "off" level
        else:
            rmn,roff=mn,noise
        spb0=BIT_US/dt
        found=None
        # sweep threshold within the LOCAL on/off band, samples-per-bit, sub-sample phase
        for frac in (0.30,0.40,0.45,0.50,0.55,0.60,0.70):
            thresh=int(rmn+frac*(roff-rmn))
            for spb in (spb0*0.98, spb0*0.99, spb0, spb0*1.01, spb0*1.02):
                for phase in [p*0.5 for p in range(0, int(2*spb)+1)]:
                    bits=recover(vals,dt,thresh,spb,phase)
                    if len(bits)<60: continue
                    # quick screen: need both 0s and 1s and some run variety
                    if sum(bits)<10 or sum(bits)>len(bits)-10: continue
                    ook=os.path.join(outdir,f"b{bi}.ook")
                    bits_to_ook(bits,ook)
                    res=decode(ook)
                    if res: found=(frac,spb,phase,res); break
                if found: break
            if found: break
        if found:
            frac,spb,phase,res=found
            print(f"\n*** burst {bi}: DECODE  frac={frac} spb={spb:.2f} phase={phase} ***")
            for l in res: print("   ",l)
        else:
            print(f"burst {bi}: min={mn}({-mn/2:.0f}dBm) no decode across sweep")
    print(f"\n.ook files in {outdir}")

if __name__=="__main__":
    main()
