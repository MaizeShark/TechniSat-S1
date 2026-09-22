#!/usr/bin/env python3
"""
Analyze a vfd_bitmap.json produced by vfd_map.py.

Confirmed packing format (from manual mapping across 2+ segments, ~87/89
data points matching):

    payload byte 1                 = header/reserved byte, always dead
    payload bytes [2+5s .. 6+5s]   = segment s's 5 bytes (s = 0..15)

Within a segment's 5 bytes (indexed 0..4 locally), bits pack as a
continuous stream, LSB-first per byte:

    stream = local_byte * 8 + bit          # 0..39
    if stream >= 35: dead (5 unused padding bits at the end of each segment)
    else:
        col = 5 - (stream % 5)             # counts down 5,4,3,2,1
        row = 7 - (stream // 5)            # counts down 7,6,5,4,3,2,1

i.e. each segment is a 5-wide x 7-tall grid, filled column-by-column,
top-to-bottom within a column, right-to-left across columns.

Usage:
    python3 analyze_map.py vfd_bitmap.json
"""
import argparse
import json


def payload_byte_to_segment(payload_byte):
    """Return (segment, local_byte 0..4) or None if this is the header byte."""
    if payload_byte < 2:
        return None
    rel = payload_byte - 2
    seg = rel // 5
    local_byte = rel % 5
    return seg, local_byte


def predict(payload_byte, bit):
    """Return (segment, col, row) or None (dead bit)."""
    loc = payload_byte_to_segment(payload_byte)
    if loc is None:
        return None
    seg, local_byte = loc
    stream = local_byte * 8 + bit
    if stream >= 35:
        return None
    col = 5 - (stream % 5)
    row = 7 - (stream // 5)
    return (seg, col, row)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("json_path")
    args = ap.parse_args()

    with open(args.json_path) as f:
        data = json.load(f)

    total = 0
    matches = 0
    mismatches = []

    for key, val in sorted(data.items(), key=lambda kv: (int(kv[0].split(":")[0]), int(kv[0].split(":")[1]))):
        if val.get("error"):
            continue
        byte_s, bit_s = key.split(":")
        byte, bit = int(byte_s), int(bit_s)
        pred = predict(byte, bit)
        total += 1

        if not val.get("lit"):
            if pred is None:
                matches += 1
            else:
                mismatches.append((byte, bit, "expected dead, predicted LIVE", pred))
            continue

        observed = (val["col"], val["row"])
        if pred is None:
            mismatches.append((byte, bit, "expected LIVE, predicted dead", observed))
            continue

        seg, pcol, prow = pred
        if (pcol, prow) == observed:
            matches += 1
        else:
            mismatches.append((byte, bit, f"predicted seg{seg} col{pcol} row{prow}", observed))

    print(f"{matches}/{total} match the confirmed packing formula.\n")
    if mismatches:
        print("Mismatches:")
        for byte, bit, note, observed in mismatches:
            print(f"  byte {byte:2d} bit {bit}: {note}, observed={observed}")
    else:
        print("No mismatches — formula fits all recorded data.")


if __name__ == "__main__":
    main()
