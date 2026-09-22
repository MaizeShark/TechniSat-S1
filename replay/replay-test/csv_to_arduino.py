#!/usr/bin/env python3
"""
csv_to_arduino.py

Converts a Saleae Logic I2C analyzer CSV export into:
  1. i2c_capture.h   - a C array of transactions (address, direction, data bytes, delay-before-this-txn)
  2. i2c_replay.ino  - an Arduino/ESP32 sketch that replays those transactions over Wire

CSV columns expected (Saleae I2C analyzer export):
  name,type,start_time,duration,data,ack,address,read

Rows of type "start" and "stop" mark transaction boundaries.
A row of type "address" gives the target address (as 0bxxxxxxxx) and the
direction ("read" column: true = read, false = write).
Rows of type "data" give one byte each, in order, for the current transaction.

Usage:
  python3 csv_to_arduino.py input.csv -o output_dir
"""

import argparse
import csv
import sys
from pathlib import Path

MAX_TXN_BYTES = 8192  # sanity ceiling; real captures can legitimately have
                       # long burst reads/writes (hundreds to low thousands
                       # of bytes), so this is just a guard against genuinely
                       # broken CSVs (e.g. missing stop rows causing runaway
                       # growth), not a byte-count assumption about your bus.


def parse_binary_field(s):
    """Parse strings like '0b01010001' or '0b00000000' into an int. Returns None if empty."""
    if s is None:
        return None
    s = s.strip()
    if s == "":
        return None
    if s.lower().startswith("0b"):
        return int(s, 2)
    # fall back: allow plain decimal or 0x hex just in case
    if s.lower().startswith("0x"):
        return int(s, 16)
    return int(s)


def parse_bool_field(s):
    if s is None:
        return None
    s = s.strip().lower()
    if s == "":
        return None
    return s == "true"


def read_transactions(csv_path):
    """
    Groups the flat row stream into transactions, splitting on EVERY
    start/repeated-start, not just on stop. This matters because a very
    common I2C pattern is:

        START, address(write), data(reg pointer), REPEATED-START,
        address(read), data, data, ..., STOP

    which is logically TWO operations (a write, then a read) sharing one
    bus transaction. If we collapsed this into a single struct we'd lose
    the write payload and mislabel the whole thing as a read. So: each
    'start' row (whether a true start or a repeated start) begins a new
    entry in the list. A 'stop' just closes out whatever is currently open.

    Each transaction is a dict:
      {
        'start_time': float,
        'address': int (7-bit),
        'is_read': bool,
        'addr_ack': bool,
        'bytes': [int, ...],
        'byte_acks': [bool, ...],
      }
    """
    transactions = []
    current = None

    with open(csv_path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rtype = row["type"].strip().lower()
            start_time = float(row["start_time"]) if row["start_time"] else None

            if rtype == "start":
                # Close out whatever was open (handles back-to-back "start" rows
                # that Saleae sometimes emits as two events for one physical START,
                # as well as genuine repeated-starts) and begin a fresh entry.
                if current is not None and current.get("address") is not None:
                    transactions.append(current)
                current = {
                    "start_time": start_time,
                    "address": None,
                    "is_read": None,
                    "addr_ack": None,
                    "bytes": [],
                    "byte_acks": [],
                }

            elif rtype == "address":
                addr = parse_binary_field(row["address"])
                is_read = parse_bool_field(row["read"])
                ack = parse_bool_field(row["ack"])
                if current is None:
                    current = {
                        "start_time": start_time,
                        "bytes": [],
                        "byte_acks": [],
                    }
                if current.get("address") is not None:
                    # a second address row without an intervening start row
                    # (shouldn't normally happen) - flush and start fresh
                    transactions.append(current)
                    current = {
                        "start_time": start_time,
                        "bytes": [],
                        "byte_acks": [],
                    }
                current["address"] = addr
                current["is_read"] = is_read
                current["addr_ack"] = ack
                if current.get("start_time") is None:
                    current["start_time"] = start_time

            elif rtype == "data":
                if current is None:
                    continue  # stray data with no transaction context, skip
                b = parse_binary_field(row["data"])
                ack = parse_bool_field(row["ack"])
                current["bytes"].append(b if b is not None else 0)
                current["byte_acks"].append(ack)
                if len(current["bytes"]) > MAX_TXN_BYTES:
                    print(
                        f"ERROR: transaction starting at start_time="
                        f"{current.get('start_time')} address="
                        f"{current.get('address')} has grown past "
                        f"{MAX_TXN_BYTES} data bytes without a 'stop' row. "
                        "This means a stop/start boundary is missing or "
                        "malformed in the CSV around this point - check the "
                        "raw file near this timestamp.",
                        file=sys.stderr,
                    )
                    sys.exit(1)

            elif rtype == "stop":
                if current is not None and current.get("address") is not None:
                    transactions.append(current)
                current = None

            # any other row types are ignored

    # handle a dangling transaction with no trailing stop row
    if current is not None and current.get("address") is not None:
        transactions.append(current)

    return transactions


def emit_header(transactions, header_name="i2c_capture.h"):
    # Sanity-check transaction sizes before emitting anything. A transaction
    # with a huge byte count (hundreds+) almost always means the CSV had a
    # missing/malformed "stop" row somewhere, so the parser kept appending
    # "data" rows into one giant transaction across what should have been
    # several separate ones. Rather than silently emit something that
    # overflows uint8_t (and corrupts the whole struct init), flag it.
    oversized = [
        (idx, txn) for idx, txn in enumerate(transactions)
        if len(txn["bytes"]) > MAX_TXN_BYTES
    ]
    if oversized:
        print(
            f"ERROR: {len(oversized)} transaction(s) exceed {MAX_TXN_BYTES} bytes "
            "(uint8_t overflow). This almost always means a 'stop' row is "
            "missing/malformed in your CSV, so multiple real transactions got "
            "merged into one giant blob. Details:",
            file=sys.stderr,
        )
        for idx, txn in enumerate(transactions):
            if len(txn["bytes"]) > MAX_TXN_BYTES:
                print(
                    f"  transaction #{idx}: start_time={txn['start_time']:.6f}s "
                    f"address=0x{txn['address']:02X} bytes={len(txn['bytes'])}",
                    file=sys.stderr,
                )
        print(
            "Fix: open the CSV around those timestamps and check for a missing "
            "'stop' row between two transactions at the same address, or a "
            "'start' row that didn't get an accompanying 'address' row right "
            "after it (which would cause it to be silently merged forward). "
            "Re-run once the CSV rows look right.",
            file=sys.stderr,
        )
        sys.exit(1)

    lines = []
    lines.append("// Auto-generated from Saleae I2C CSV export.")
    lines.append("// Do not edit by hand - regenerate with csv_to_arduino.py")
    lines.append("#pragma once")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("typedef struct {")
    lines.append("  uint8_t address;      // 7-bit I2C address")
    lines.append("  uint8_t is_read;      // 1 = master read, 0 = master write")
    lines.append("  uint32_t delay_us;    // delay AFTER previous transaction, before this one")
    lines.append("  uint16_t len;         // number of data bytes (can exceed 255 for burst transfers)")
    lines.append("  const uint8_t *data;  // pointer into a byte pool (write payloads only)")
    lines.append("} i2c_txn_t;")
    lines.append("")

    # Emit one flat byte array per write-transaction's payload to avoid
    # a huge number of tiny arrays; each gets a unique symbol.
    byte_pool_decls = []
    txn_entries = []

    prev_start = None
    for idx, txn in enumerate(transactions):
        addr = txn["address"] if txn["address"] is not None else 0
        is_read = 1 if txn.get("is_read") else 0
        data_bytes = txn["bytes"]

        if prev_start is None:
            delay_us = 0
        else:
            delay_us = max(0, int(round((txn["start_time"] - prev_start) * 1_000_000)))
        prev_start = txn["start_time"]

        if data_bytes:
            arr_name = f"txn_{idx}_data"
            byte_str = ", ".join(f"0x{b:02X}" for b in data_bytes)
            byte_pool_decls.append(f"static const uint8_t {arr_name}[] = {{ {byte_str} }};")
            data_ref = arr_name
        else:
            data_ref = "NULL"

        txn_entries.append(
            f"  {{ 0x{addr:02X}, {is_read}, {delay_us}u, {len(data_bytes)}, {data_ref} }},"
        )

    lines.extend(byte_pool_decls)
    lines.append("")
    lines.append(f"#define I2C_TXN_COUNT {len(transactions)}")
    lines.append("")
    lines.append("static const i2c_txn_t i2c_txns[I2C_TXN_COUNT] = {")
    lines.extend(txn_entries)
    lines.append("};")
    lines.append("")

    return "\n".join(lines)


def emit_sketch():
    return '''// Auto-generated replay sketch.
// Replays captured I2C transactions from i2c_capture.h over Wire.
//
// Notes:
// - Write transactions are replayed verbatim (address + data bytes).
// - Read transactions issue a Wire.requestFrom() of the same length that
//   was captured. The actual bytes read back from your live device will
//   likely differ from what was originally captured (that's expected -
//   reads reflect current device state, not the recording).
// - delay_us is the gap between the START of this transaction and the
//   START of the previous one, taken directly from the capture timestamps.
//   Adjust REPLAY_SPEED below if you want it faster/slower than realtime.

#include <Wire.h>
#include "i2c_capture.h"

// 1.0 = realtime (as captured), 0.5 = 2x faster, 2.0 = 2x slower, 0.0 = no delay at all
#define REPLAY_SPEED 1.0f

// Set to your ESP32 I2C pins if not using defaults
#define SDA_PIN 21
#define SCL_PIN 22
#define I2C_CLOCK_HZ 400000

void replayTransaction(const i2c_txn_t &txn) {
  if (txn.is_read) {
    // NOTE: ESP32's Wire buffer defaults to 128 bytes. Large burst reads
    // (this capture includes some multi-KB transactions) must be chunked,
    // and/or you must call Wire.setBufferSize(n) in setup() BEFORE Wire.begin()
    // with a size >= the largest txn.len you have, e.g. Wire.setBufferSize(4096).
    uint16_t remaining = txn.len;
    const uint16_t CHUNK = 128; // matches default Wire buffer; raise if you called setBufferSize()
    while (remaining > 0) {
      uint16_t n = remaining > CHUNK ? CHUNK : remaining;
      Wire.requestFrom((int)txn.address, (int)n);
      while (Wire.available()) {
        volatile uint8_t b = Wire.read(); // discard / or log via Serial
        (void)b;
      }
      remaining -= n;
    }
  } else {
    if (txn.len > 4096) {
      Serial.printf("WARNING: write txn len=%d exceeds Wire buffer (4096) - "
                    "increase Wire.setBufferSize() in setup(), this write will be truncated.\n",
                    txn.len);
    }
    Wire.beginTransmission(txn.address);
    for (uint16_t i = 0; i < txn.len; i++) {
      Wire.write(txn.data[i]);
    }
    Wire.endTransmission();
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // IMPORTANT: must be called BEFORE Wire.begin(). Match/exceed the largest
  // txn.len in i2c_capture.h, or large writes (Wire.write in a burst) will
  // silently truncate. Chunked reads above work regardless, but writes do not
  // currently chunk - raise this if you see write truncation.
  Wire.setBufferSize(4096);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ);

  Serial.printf("Replaying %d I2C transactions...\\n", I2C_TXN_COUNT);

  for (int i = 0; i < I2C_TXN_COUNT; i++) {
    const i2c_txn_t &txn = i2c_txns[i];

    if (txn.delay_us > 0 && REPLAY_SPEED > 0.0f) {
      uint32_t d = (uint32_t)(txn.delay_us * REPLAY_SPEED);
      if (d > 0) delayMicroseconds(d > 16000 ? 16000 : d); // cap per-call, loop if needed
      if (d > 16000) {
        uint32_t remaining = d - 16000;
        while (remaining > 0) {
          uint32_t chunk = remaining > 16000 ? 16000 : remaining;
          delayMicroseconds(chunk);
          remaining -= chunk;
        }
      }
    }

    Serial.printf("[%d] addr=0x%02X %s len=%d\\n", i, txn.address,
                  txn.is_read ? "READ" : "WRITE", txn.len);

    replayTransaction(txn);
  }

  Serial.println("Replay complete.");
}

void loop() {
  // Replay runs once in setup(). Add looping/repeat logic here if desired.
}
'''


def main():
    ap = argparse.ArgumentParser(description="Convert Saleae I2C CSV export to Arduino replay files")
    ap.add_argument("csv_file", help="Path to input CSV file")
    ap.add_argument("-o", "--outdir", default=".", help="Output directory (default: current dir)")
    args = ap.parse_args()

    csv_path = Path(args.csv_file)
    if not csv_path.exists():
        print(f"Error: {csv_path} not found", file=sys.stderr)
        sys.exit(1)

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    transactions = read_transactions(csv_path)
    if not transactions:
        print("Warning: no complete transactions found (need start+address+stop rows).", file=sys.stderr)

    header_path = outdir / "i2c_capture.h"
    sketch_path = outdir / "i2c_replay.ino"

    header_path.write_text(emit_header(transactions))
    sketch_path.write_text(emit_sketch())

    max_len = max((len(t["bytes"]) for t in transactions), default=0)
    max_delay_s = max(
        ((transactions[i]["start_time"] - transactions[i - 1]["start_time"])
         for i in range(1, len(transactions))),
        default=0,
    )

    print(f"Parsed {len(transactions)} transactions.")
    print(f"Largest single transaction: {max_len} bytes"
          + (" -> update Wire.setBufferSize() in the sketch if this exceeds 4096."
             if max_len > 4096 else ""))
    if max_delay_s > 1.0:
        print(f"Note: largest gap between transactions is {max_delay_s:.3f}s "
              "- this will be replayed as a real delay unless you lower REPLAY_SPEED "
              "or edit delay_us values in i2c_capture.h.")
    print(f"Wrote {header_path}")
    print(f"Wrote {sketch_path}")
    print("\nCopy both files into the same Arduino sketch folder"
          " (folder name should match i2c_replay.ino, e.g. rename the folder to 'i2c_replay').")


if __name__ == "__main__":
    main()