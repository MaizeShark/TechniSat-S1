> [!NOTE]
> **Hardware Scope Limitation**
> 
> The information documented here is exclusively for the **`TechniSat Digit Isio S`**.
> 
> This does **not** apply to the `Digicorder Isio S` model, which is a physically wider unit with more buttons and is presumed to use a different PCB layout. A `Digicorder` unit is not available to me for analysis.

# Front Panel Controller (I²C `0x70`)

The front panel is a self-contained subsystem. A single microcontroller drives
the VFD, scans the buttons, decodes the IR remote and controls the status LED,
and it exposes all of that to the mainboard SoC over one I²C bus at address
`0x70`. The SoC never touches the display grids or the IR waveform directly.

Everything below was reverse-engineered from logic-analyser captures of the
original SoC's traffic, then re-verified by driving a detached front panel from
an ESP32. Anything that was *only* observed in a capture and never confirmed
against live hardware is marked as such.

For the PCB-level pinout, FFC wiring and component placement, see
**[Front PCB](Front-PCB.md)**.

---

## 1. Hardware

| Item           | Detail                                                                                                                                         |
|:-------------- |:---------------------------------------------------------------------------------------------------------------------------------------------- |
| Controller     | `TMP87CH75FG-6CP9` — Toshiba TLCS-870 family, with an integrated VFD driver ([datasheet](../datasheets/TMP87CH75FG_datasheet_en_20080306.pdf)) |
| Host interface | I²C slave, 7-bit address `0x70`                                                                                                                |
| Display        | VFD, 16 characters × 1 line, 5×7 dot matrix per character                                                                                      |
| Buttons        | 6 — Up, Down, Left, Right, OK, Power                                                                                                           |
| LED            | Single indicator, two colours (blue / red), software-selectable                                                                                |
| IR             | Receiver with **on-chip protocol decoding** — the host only ever sees a button code                                                            |
| Logic level    | 3V3 (I²C)                                                                                                                                      |
| Supply         | VDD = 5V                                                                                                                                       |

### Interrupt line

The controller signals "a report is waiting" by pulling a dedicated line low;
the host then does a 4-byte read from `0x70`. On the ESP32 side this is wired to
a GPIO configured as `INPUT_PULLUP` with a falling-edge interrupt.

The FFC carries three general-purpose MCU pins (`P20`, `P21`, `P22`) whose roles
are not individually confirmed. `P21` has a 5 kΩ pull-up on the front PCB, which
would fit an open-drain interrupt output, and `P20`'s datasheet alternate
function is `INT5` — but which of the three actually carries the report
interrupt has not been verified by probing. See [Front PCB](Front-PCB.md).

---

## 2. I²C protocol

The controller accepts exactly two shapes of write, distinguished by the first
byte, and produces exactly one shape of read.

| Direction | Length   | Shape                                                                                    |
|:--------- |:-------- |:---------------------------------------------------------------------------------------- |
| Write     | 5 bytes  | `[cmd][b1][b2][b3][b4]` — short command (`0xE7`, `0xE8`, `0xEA`, `0xEB`, `0xED`, `0xEE`) |
| Write     | 82 bytes | `[0x10][81-byte payload]` — full display frame                                           |
| Read      | 4 bytes  | `[type][..][..][..]` — input report                                                      |

The reference driver runs the bus at **100 kHz**. Nothing is known to require a
specific clock rate.

### 2.1 Initialisation

Three commands, sent once after power-on, are required for the **input** side to
work. The display side needs no initialisation at all — display frames are
accepted and rendered correctly on a cold device with no preceding writes.

| Command          | Effect                            |
|:---------------- |:--------------------------------- |
| `EA FF FF 88 01` | required for button reporting     |
| `EA 0F 00 00 00` | required for button reporting     |
| `E8 08 0A 00 00` | additionally enables IR reporting |

Roughly 20 ms between commands, matching the spacing in the original capture.

The original SoC sent **eleven** commands at startup. Each was tested
individually against a working baseline to find the minimum. Of the other eight:

- `E7 A0 00 00 00` — just brightness level 7 (see [§2.3](#23-brightness))
- `ED 05 15 00 00` — just LED blue / display power on (see [§2.4](#24-led-and-display-power))
- `EE 06 00 00 00`, `EB 14 0C 00 01`, `E8 08 0A 36 0D`, `ED 04 23 7F 01`,
  `E8 08 FF 00 00`, `ED 10 01 00 00` — each made **no observed difference** to
  button, IR, VFD or LED behaviour when tested in isolation.

Those six remain unexplained rather than proven useless — they may matter for
something not yet exercised. They are kept commented out in
`VfdController::initDevice()` rather than deleted.

Before the button-enable commands take effect, reads return a repeating
`03 FA 00 00`. It is stable and non-garbage-looking, but its meaning is unknown.
The reference driver discards unrecognised report types.

### 2.2 Display frames

One 82-byte write paints the entire 16-character display. There is no partial
update and no cursor.

```
byte 0        0x10          command
byte 1        0x00          reserved — always 0x00 in every capture
bytes 2..6    segment 0     5 bytes
bytes 7..11   segment 1     5 bytes
...
bytes 77..81  segment 15    5 bytes
```

Segment *s* occupies payload bytes `1 + 5s .. 5 + 5s`, counting the payload from
the byte after `0x10`.

#### Bit-to-pixel mapping

A character cell is 5 columns × 7 rows = 35 pixels, packed into 5 bytes (40
bits) as a **continuous LSB-first bit stream across all five bytes** — bit
positions run straight through byte boundaries rather than restarting per byte.

For stream index `p = byte_index × 8 + bit_index`, where `byte_index` is 0..4
within the segment:

```
row    = p / 5        0 = top row,      6 = bottom row
column = 4 - (p % 5)  0 = leftmost col, 4 = rightmost col
```

So the stream fills the cell **right-to-left within each row, top row first**.
The final 5 bits (`p = 35..39`) are unused padding.

```
       col 0   col 1   col 2   col 3   col 4
row 0    p4      p3      p2      p1      p0
row 1    p9      p8      p7      p6      p5
row 2   p14     p13     p12     p11     p10
row 3   p19     p18     p17     p16     p15
row 4   p24     p23     p22     p21     p20
row 5   p29     p28     p27     p26     p25
row 6   p34     p33     p32     p31     p30
```

This mapping was derived empirically rather than from a datasheet. A firmware
command lit exactly one bit at a time while
[`frontpanel/tools/vfd_map.py`](../../frontpanel/tools/vfd_map.py) walked all 640
(byte, bit) pairs and the operator recorded which pixel lit up; `analyze_map.py`
then checked the collected observations against the formula above. The raw
results are kept in `vfd_bitmap.json`.

> [!IMPORTANT]
> The Python tools are **stale** with respect to the current demo firmware. They
> drive it over serial using commands `k<byte>=<bit>` (light one bit), `i`
> (init) and `x` (send raw frame), none of which `src/main.cpp` implements any
> more — its console now offers `c`, `r`, `br`, `ct`, `sc`, `fill`, `clear`,
> `ledblue`/`ledred` and `pwon`/`pwoff` instead. Those three commands need
> re-adding before the tools will run again. The mapping they produced is
> already folded into the driver, so this only matters if you want to re-derive
> or extend it.

### 2.3 Brightness

```
E7 XX 00 68 B3      XX = 0x30 + level × 0x10,  level 0..7
```

`0x30` is dimmest, `0xA0` brightest — eight distinct, stable levels, confirmed
against real hardware. Eight is exactly what the silicon offers: the datasheet
states "brightness level can be adjusted in 8 steps using the dimmer function",
via the 3-bit `DIM` field of the `VFTCR2` register.

The original capture's startup used the trailing bytes `00 00 00` instead of
`00 68 B3` (`E7 A0 00 00 00`); both forms work, so the last three bytes appear
not to be significant for brightness.

### 2.4 LED and display power

Both live in the `ED 05` command family and are confirmed against real hardware.
LED colour was pinned down by capturing the original SoC's Power-button handling
and correlating each command with the LED colour observed at that instant.

| Command          | Effect                          |
|:---------------- |:------------------------------- |
| `ED 05 15 00 00` | LED blue / display power **on** |
| `ED 05 13 00 00` | LED red                         |
| `ED 05 23 00 00` | display power **off**           |

LED colour is independent of brightness and of displayed content — setting it
does not dim, blank or clear anything.

Display power **off** cuts the VFD's HV supply: measured draw falls from ~0.2 A
to ~0.011 A. It also clears the displayed content — turning back on does *not*
restore the previous frame, so the host must send a fresh one. Because "on" and
"LED blue" are the same command, waking the display also resets the LED to blue.

Two behaviours from the original capture worth knowing:

- The SoC re-sends the **off** command roughly every second while the display is
  off, which looks like a keepalive or watchdog ping. If a detached panel seems
  to wake on its own after a while, try repeating the off command.
- Waking from off was preceded by `ED 10 01 00 00` immediately before the fresh
  frame write. Its purpose is unconfirmed, but it is worth trying if a wake does
  not fully restore the display.

There is no known command to *read back* LED colour or power state, so a host
driver can only track what it last set.

### 2.5 Input reports

When the interrupt line asserts, the host reads 4 bytes from `0x70`. Byte 0 tags
the report type.

#### Buttons — type `0x02`

```
[0x02][mask1][mask2][state]
```

`state` (byte 3): `0x00` = press, `0xFF` = release.

| Button | Byte  | Mask   |
|:------ |:-----:|:------:|
| Right  | mask1 | `0x01` |
| Down   | mask1 | `0x08` |
| Left   | mask1 | `0x20` |
| OK     | mask1 | `0x40` |
| Up     | mask1 | `0x80` |
| Power  | mask2 | `0x01` |

#### IR remote — type `0x01`

```
[0x01][code][state][0x08]
```

Note that `state` is **byte 2** here, not byte 3 as in button reports: `0x00` =
press, `0xFF` = release. Byte 3 is always `0x08`; its purpose is unknown.

The controller fully decodes the IR protocol on-chip. A different (non-matching)
remote produced **no I²C traffic whatsoever**, so raw IR timing is not reachable
through this interface — the host only ever sees a decoded button code.

---

## 3. IR remote keymap

Every code below was confirmed by pressing the physical remote button and
reading back the decoded value. Note this was tested through the reference
driver, not by observing the original SoC.

| Code          | Button      | Code   | Button     | Code   | Button     |
|:-------------:|:----------- |:------:|:---------- |:------:|:---------- |
| `0x00`–`0x09` | `0`–`9`     | `0x34` | Forward    | `0x69` | NAV        |
| `0x0A`        | OPT         | `0x35` | Play/Pause | `0x6A` | Timer      |
| `0x0C`        | Power       | `0x36` | Stop       | `0x6B` | Red        |
| `0x0D`        | Mute        | `0x37` | Record     | `0x6C` | Green      |
| `0x10`        | Volume +    | `0x3C` | TXT        | `0x6D` | Yellow     |
| `0x11`        | Volume −    | `0x4F` | Info       | `0x6E` | Blue       |
| `0x20`        | Program +   | `0x50` | Up         | `0x74` | Page +     |
| `0x21`        | Program −   | `0x51` | Down       | `0x75` | Page −     |
| `0x22`        | Back / EXIT | `0x52` | Menu       | `0x78` | Genre      |
| `0x23`        | Audio       | `0x53` | TV/Radio   | `0x79` | HDMI       |
| `0x26`        | Sleep       | `0x55` | Left       | `0x7D` | Help       |
| `0x29`        | Still       | `0x56` | Right      | `0x7E` | Zoom       |
| `0x2F`        | SFI         | `0x57` | OK         | `0x7F` | WWW / MORE |
| `0x32`        | Reverse     | `0x58` | PiP        |        |            |

Two remotes were tested against this device. `0x22` is labelled **Back** on one
and **EXIT** on the other; `0x7F` is **WWW** on one and **MORE** on the other.
`0x78` (Genre) and `0x7D` (Help) appear only on the second remote.

> [!NOTE]
> **`0x3F` is not a button.** It appears while cycling the remote's
> code/protocol-select function (hold *code*, press *OK* to cycle through three
> protocol modes — only one of which this receiver understands). It is most
> likely a protocol identifier or handshake, so treat a raw `0x3F` as "the
> receiver saw something, probably a mode-select artifact", not as a keypress.

---

## 4. Open questions

- Which FFC pin actually carries the report interrupt (`P20`, `P21` or `P22`).
- The six unexplained startup commands in [§2.1](#21-initialisation).
- The `03 FA 00 00` report returned before button reporting is enabled.
- What `ED 10 01 00 00` does on wake.

---

## 5. Reference implementation

A working ESP32 driver for everything documented above lives in
[`frontpanel/firmware`](../../frontpanel/firmware):

| Path                 | Contents                                                                                                      |
|:-------------------- |:------------------------------------------------------------------------------------------------------------- |
| `lib/VfdController/` | The driver — I²C protocol, frame encoding, input decoding, 5×7 UTF-8 font with German umlauts, scrolling text |
| `src/main.cpp`       | Demo firmware with a serial console for sending text and probing raw commands                                 |
| `test/`              | Generated sketch that replays a captured I²C session verbatim                                                 |
| `csv_to_arduino.py`  | Converts a Saleae I²C CSV export into that replay sketch                                                      |

The serial console in `src/main.cpp` is the main tool for probing unknown
commands: `c E7 30 2A 38 80` sends an arbitrary 5-byte command, and `r` does a
raw 4-byte read independently of the interrupt line.

Bit-mapping tools are in [`frontpanel/tools`](../../frontpanel/tools) — see the
caveat in [§2.2](#bit-to-pixel-mapping) about them being out of sync with the
current firmware.

The raw Saleae captures this was derived from are kept locally under
`captures/i2c/` but are **not** in the repository — they are bulky and noisy,
and their conclusions are recorded on this page instead.
