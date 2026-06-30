#!/usr/bin/env python3
"""Resilient serial logger for the overnight coverage test.

Captures every serial line (host-timestamped) to a file, reconnecting on any error so a USB
hiccup doesn't lose the run. Does NOT toggle DTR/RTS, so opening the port won't reset the board.

Usage:  python3 coverage_logger.py <output.log> [port]
Run under caffeinate so the Mac stays awake:
        caffeinate -ims python3 coverage_logger.py results/coverage-YYYYMMDD.log
"""
import serial, time, sys, datetime

PORT = sys.argv[2] if len(sys.argv) > 2 else "/dev/cu.usbserial-59260134191"
OUT  = sys.argv[1] if len(sys.argv) > 1 else "results/coverage.log"

def note(f, msg):
    f.write(f"# {datetime.datetime.now().isoformat()} {msg}\n"); f.flush()

while True:
    try:
        s = serial.Serial()
        s.port = PORT; s.baudrate = 115200; s.timeout = 2
        s.dtr = False; s.rts = False          # don't reset the board on open
        s.open()
        with open(OUT, "a", buffering=1) as f:
            note(f, f"logger connected to {PORT}")
            while True:
                line = s.readline().decode("utf-8", "replace").rstrip()
                if line:
                    f.write(f"{time.time():.3f} {line}\n")
    except Exception as e:
        try:
            with open(OUT, "a", buffering=1) as f:
                note(f, f"logger error: {e} (reconnecting)")
        except Exception:
            pass
        time.sleep(3)
