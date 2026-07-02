#!/usr/bin/env python3
"""Capture LoRa32 OLED screenshots as PNGs (documentation helper).

Flash the device with the screenshot build first:
    pio run -e screenshot -t upload
Then run this and navigate the device through the screens you want:
    python experiments/capture_screens.py --port /dev/cu.usbserial-XXești --out docs/img

The firmware dumps its 128x64 framebuffer over serial every time the OLED image
changes (see `-DSCREENSHOT`). This script rebuilds each distinct frame into a PNG.
Every new screen is auto-saved as screen-NN.png; curate/rename afterwards.

Requires: pyserial, Pillow  (pip install pyserial pillow)

When you're done capturing, re-flash the normal firmware:
    pio run -e optionb -t upload
"""
import argparse, os, sys, time

def frame_to_image(data, scale, invert):
    from PIL import Image, ImageOps
    W, H = 128, 64
    img = Image.new("L", (W, H), 0)
    px = img.load()
    for page in range(H // 8):                 # 8 vertical pages of 8 px each
        for x in range(W):
            byte = data[page * W + x]
            for bit in range(8):               # bit 0 = topmost pixel of the page
                if (byte >> bit) & 1:
                    px[x, page * 8 + bit] = 255 # lit OLED pixel -> white
    if invert:
        img = ImageOps.invert(img)             # -> black-on-white for docs
    if scale != 1:
        img = img.resize((W * scale, H * scale), Image.NEAREST)
    return img

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="serial device, e.g. /dev/cu.usbserial-59260134191")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", default="screens", help="output directory (default: screens/)")
    ap.add_argument("--scale", type=int, default=4, help="pixel scale factor (default 4 -> 512x256)")
    ap.add_argument("--invert", action="store_true", help="black-on-white instead of white-on-black")
    args = ap.parse_args()

    try:
        import serial  # pyserial
        from PIL import Image  # noqa: F401  (import check)
    except ImportError as e:
        sys.exit(f"missing dependency: {e}. Run: pip install pyserial pillow")

    os.makedirs(args.out, exist_ok=True)
    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = args.baud
    ser.timeout = 1
    ser.dtr = False          # try not to reset the ESP32 on open (best-effort; some adapters still pulse)
    ser.rts = False
    ser.open()
    print(f"listening on {args.port} @ {args.baud} — navigate the device to each screen you want.")
    print("(the board may reboot once when the port opens; that's expected)")
    print("Ctrl-C to stop.\n")

    n = 0
    collecting = False
    hexbuf = []
    try:
        while True:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").strip()
            if line.startswith("---SCREEN"):
                collecting = True
                hexbuf = []
            elif line.startswith("---ENDSCREEN---"):
                collecting = False
                hexstr = "".join(hexbuf)
                try:
                    data = bytes.fromhex(hexstr)
                except ValueError:
                    print("  (skipped a corrupt frame)")
                    continue
                if len(data) != 1024:
                    print(f"  (skipped frame: {len(data)} bytes, expected 1024)")
                    continue
                img = frame_to_image(data, args.scale, args.invert)
                path = os.path.join(args.out, f"screen-{n:02d}.png")
                img.save(path)
                print(f"  saved {path}  ({img.width}x{img.height})")
                n += 1
            elif collecting:
                hexbuf.append(line)
    except KeyboardInterrupt:
        print(f"\ndone — {n} screenshot(s) in {args.out}/")
    finally:
        ser.close()

if __name__ == "__main__":
    main()
