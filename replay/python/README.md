# VFD bit-to-pixel mapping tool

Two small scripts to reverse-engineer which bit in the 80-byte VFD frame
payload controls which physical pixel.

## Setup

```
pip install pyserial
```

Flash the ESP32 firmware (from the `vfd-replay` project) first, and run
`i` once in the Serial Monitor to initialize the device — or just let this
tool talk to it after you've done that manually. Then **close the Serial
Monitor / any other program using the serial port** before running this
tool, since only one program can hold the port open at a time.

## Usage

```
python3 vfd_map.py --port /dev/ttyUSB0
```

(On Windows, use something like `--port COM5`.)

It will step through every byte (1-80) and bit (0-7), 640 total, sending
one `k<byte>=<bit>` command at a time to the ESP32, and ask you what you
observed:

```
byte  9 bit 0 [65/640] >
```

Enter:
- `col,row` — e.g. `4,4` — if a pixel lit up, using top-down, left-to-right
  coordinates within whichever segment lit up (note: this tool doesn't
  currently ask you *which segment* — if you want to track that too, just
  mention it and I'll add a segment field)
- *(nothing, just Enter)* — if nothing lit up
- `s` — skip this one for now (e.g. unsure), revisit later
- `b<N>` — jump straight to byte N, bit 0 (e.g. `b20`) — handy for skipping
  ahead past a block of already-tested or known-dead bytes
- `q` — save progress and quit; rerun the same command later to resume
  right where you left off (already-answered entries are skipped
  automatically)

Progress is saved continuously to `vfd_bitmap.json` (or wherever you point
`--save`), so a crash or Ctrl+C never loses more than the current entry.

## Analyzing results

```
python3 analyze_map.py vfd_bitmap.json
```

This checks your collected data against the simplest hypothesis we've
derived so far (column counts down 5,4,3,2,1 as the bit-stream advances,
wrapping to the row above every 5 bits) and reports which entries match.
Mismatches are useful too — they tell us where the simple model breaks
down (e.g. at segment boundaries), which is exactly what we need to refine
the formula.

If you want to try a different starting anchor point, pass:

```
python3 analyze_map.py vfd_bitmap.json --origin-byte 9 --origin-bit 0 --origin-col 4 --origin-row 4
```
