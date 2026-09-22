#!/usr/bin/env python3
"""
Send a VFD frame to the ESP32 over serial, using the firmware's 'x' command
-- no reflashing needed. Requires the firmware's 'i' (init) to have been
run at least once since power-on (either manually in Serial Monitor, or
this script can send it for you with --init).

Usage:
    # Show some text using the built-in tiny font:
    python3 send_frame.py --port /dev/ttyUSB0 --text "HELLO"

    # Or build your own frame in Python and pipe it in:
    python3 -c "
from vfd_encoder import encode_frame, text_to_segments
frame = encode_frame(text_to_segments('HELLO'))
print(' '.join(f'{b:02X}' for b in frame[1:]))  # skip cmd byte, firmware adds it
" | python3 send_frame.py --port /dev/ttyUSB0 --stdin-hex

    # Initialize the device first if you haven't already this session:
    python3 send_frame.py --port /dev/ttyUSB0 --init --text "HELLO"
"""
import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("This tool needs pyserial. Install with: pip install pyserial")
    sys.exit(1)

from vfd_encoder import encode_frame, text_to_segments


def read_until_ready(ser, marker="READY", timeout=3.0):
    deadline = time.time() + timeout
    lines = []
    while time.time() < deadline:
        line = ser.readline().decode("ascii", errors="replace").strip()
        if not line:
            continue
        lines.append(line)
        print(f"  device: {line}")
        if marker in line:
            return lines
    print(f"  ! timed out waiting for '{marker}'")
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--text", help="Text to render using the built-in tiny font (see vfd_encoder.FONT_5X7)")
    ap.add_argument("--stdin-hex", action="store_true", help="Read payload hex bytes (space or no space separated) from stdin instead of --text")
    ap.add_argument("--init", action="store_true", help="Send the device init sequence first")
    args = ap.parse_args()

    if not args.text and not args.stdin_hex:
        print("Provide either --text or --stdin-hex")
        sys.exit(1)

    if args.stdin_hex:
        hex_payload = sys.stdin.read().strip()
    else:
        frame = encode_frame(text_to_segments(args.text))
        # frame[0] is the 0x10 command byte; the firmware's 'x' command adds
        # that itself, so we only send the 81 payload bytes.
        hex_payload = " ".join(f"{b:02X}" for b in frame[1:])

    # Defensive check: if whatever we're about to send is 82 bytes long and
    # starts with the 0x10 command byte, someone (script or human) likely
    # included the command byte by mistake -- strip it here too, in addition
    # to the firmware's own leniency for this case.
    compact_check = hex_payload.replace(" ", "")
    n_bytes = len(compact_check) // 2
    if n_bytes == 82 and compact_check[:2].upper() == "10":
        print("Note: 82 bytes detected starting with 0x10 -- stripping the leading command byte.")
        hex_payload = " ".join(f"{b:02X}" for b in bytes.fromhex(compact_check)[1:])

    print(f"Opening {args.port} @ {args.baud}...")
    ser = serial.Serial(args.port, args.baud, timeout=0.5)
    time.sleep(2.0)  # let ESP32 finish reset-on-connect
    ser.reset_input_buffer()

    if args.init:
        print("Sending init sequence...")
        ser.write(b"i\n")
        read_until_ready(ser, marker="Init sequence done", timeout=5.0)

    print(f"Sending frame ({len(hex_payload.replace(' ', '')) // 2} payload bytes)...")
    ser.write(f"x{hex_payload}\n".encode("ascii"))
    read_until_ready(ser, marker="READY", timeout=3.0)

    ser.close()
    print("Done.")


if __name__ == "__main__":
    main()
