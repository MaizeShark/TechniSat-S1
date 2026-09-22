#!/usr/bin/env python3
"""
Interactive VFD bit->pixel mapping tool.

Talks to the ESP32 firmware over serial, walks through every (byte, bit)
combination in the 80-byte payload, and asks you what lit up on the real
display. Saves progress to a JSON file after every entry, so you can quit
(Ctrl+C) and resume later without losing work.

Usage:
    python3 vfd_map.py --port /dev/ttyUSB0
    python3 vfd_map.py --port COM5 --baud 115200

At each step you'll be prompted like:

    byte 12 bit 3 [12/640] >

Type what you saw, in "col,row" form (e.g. "3,2" for column 3, row 2,
top-down / left-to-right, matching the convention you've been using), or:
    (empty + Enter)  -> nothing lit up
    s                -> skip (unsure / didn't check), can revisit later
    b<n>              -> jump directly to byte n, bit 0 (e.g. "b20")
    q                -> save and quit

The tool skips any (byte, bit) pair already recorded in the save file, so
resuming just continues where you left off. Use --redo to re-visit already
answered entries instead.
"""
import argparse
import json
import os
import sys
import time

try:
    import serial
except ImportError:
    print("This tool needs pyserial. Install with: pip install pyserial")
    sys.exit(1)

PAYLOAD_LEN = 80  # bytes 1..80 (byte 0 is the fixed 0x10 command byte, never touched)


def load_map(path):
    if os.path.exists(path):
        with open(path) as f:
            return json.load(f)
    return {}


def save_map(path, data):
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(data, f, indent=2, sort_keys=True)
    os.replace(tmp, path)


def send_and_wait_ready(ser, byte_idx, bit_idx, timeout=2.0):
    """Send k<byte>=<bit> and block until the READY line comes back."""
    cmd = f"k{byte_idx}={bit_idx}\n"
    ser.write(cmd.encode("ascii"))
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = ser.readline().decode("ascii", errors="replace").strip()
        if not line:
            continue
        if line.startswith("READY"):
            return True
        if "Index out of range" in line or "Usage:" in line:
            print(f"  ! device rejected command: {line}")
            return False
    print("  ! timed out waiting for device response")
    return False


def parse_observation(raw):
    """Return ('pixel', col, row) / ('none',) / ('skip',) / ('jump', byte) / ('quit',) / None (invalid)."""
    raw = raw.strip()
    if raw == "":
        return ("none",)
    if raw.lower() == "s":
        return ("skip",)
    if raw.lower() == "q":
        return ("quit",)
    if raw.lower().startswith("b") and raw[1:].isdigit():
        return ("jump", int(raw[1:]))
    if "," in raw:
        parts = raw.split(",")
        if len(parts) == 2 and all(p.strip().lstrip("-").isdigit() for p in parts):
            col, row = int(parts[0]), int(parts[1])
            return ("pixel", col, row)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="Serial port, e.g. /dev/ttyUSB0 or COM5")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--save", default="vfd_bitmap.json", help="Path to save/resume progress")
    ap.add_argument("--redo", action="store_true", help="Revisit already-answered entries too")
    ap.add_argument("--start-byte", type=int, default=1, help="Start at this byte index (default 1)")
    args = ap.parse_args()

    data = load_map(args.save)
    print(f"Loaded {len(data)} existing entries from {args.save}" if data else "Starting fresh map.")

    print(f"Opening {args.port} @ {args.baud}...")
    ser = serial.Serial(args.port, args.baud, timeout=0.5)
    time.sleep(2.0)  # let ESP32 finish reset-on-connect
    ser.reset_input_buffer()

    print()
    print("Ready. For each prompt, enter:")
    print("  <col>,<row>   e.g. '3,2'  (top-down, left-to-right, matches your earlier convention)")
    print("  <Enter>       nothing lit up")
    print("  s             skip this one for now")
    print("  b<N>          jump to byte N, bit 0")
    print("  q             save and quit")
    print()

    byte_idx = args.start_byte
    total = PAYLOAD_LEN * 8

    while byte_idx <= PAYLOAD_LEN:
        for bit_idx in range(8):
            key = f"{byte_idx}:{bit_idx}"
            if key in data and not args.redo:
                continue

            step_num = (byte_idx - 1) * 8 + bit_idx + 1
            ok = send_and_wait_ready(ser, byte_idx, bit_idx)
            if not ok:
                print("  (retrying once...)")
                ok = send_and_wait_ready(ser, byte_idx, bit_idx)
                if not ok:
                    print("  ! giving up on this entry, marking as error")
                    data[key] = {"error": True}
                    save_map(args.save, data)
                    continue

            prompt = f"byte {byte_idx:2d} bit {bit_idx} [{step_num}/{total}] > "
            raw = input(prompt)
            parsed = parse_observation(raw)

            if parsed is None:
                print("  ! didn't understand that, treating as skip. Use 'col,row' or Enter for nothing.")
                continue
            if parsed[0] == "quit":
                save_map(args.save, data)
                print(f"Saved {len(data)} entries to {args.save}. Bye!")
                ser.close()
                return
            if parsed[0] == "skip":
                continue
            if parsed[0] == "jump":
                byte_idx = parsed[1]
                break  # break inner loop, outer while will re-enter at new byte_idx
            if parsed[0] == "none":
                data[key] = {"lit": False}
            elif parsed[0] == "pixel":
                data[key] = {"lit": True, "col": parsed[1], "row": parsed[2]}

            save_map(args.save, data)
        else:
            byte_idx += 1
            continue
        # only reached via 'break' from a jump
        continue

    print(f"All {total} bit positions covered (or skipped). Saved to {args.save}.")
    ser.close()


if __name__ == "__main__":
    main()
