#pragma once
#include <Arduino.h>
#include <Wire.h>

// ---------------------------------------------------------------------------
// VfdController
//
// Driver for a custom VFD + 6-button + 2-LED + IR receiver controller chip,
// reverse-engineered from I2C capture of the original SoC's traffic.
// Confirmed protocol details:
//
//   - I2C address 0x70
//   - Init sequence: 3 short (5-byte) config writes, required once after
//     power-on for button and IR reporting to work. VFD display frames
//     work fine with NO init at all -- init is only needed for the
//     input side. Confirmed minimal by testing each of the original 11
//     captured commands individually: EA FF FF 88 01 + EA 0F 00 00 00
//     enable button reporting; E8 08 0A 00 00 additionally enables IR.
//     The other 8 original commands made no observed difference to
//     anything tested so far (still commented-in-place in
//     VfdController.cpp in case they matter for something untested,
//     e.g. LEDs).
//   - Display frame: 82-byte write, [0x10 cmd][81-byte payload].
//       payload[0]            = reserved, always 0x00 in captures
//       payload[1+5s .. 5+5s] = segment s's 5 bytes, s=0..15
//     Within a segment (5 cols x 7 rows = 35 pixels), bits pack as a
//     continuous LSB-first stream across the 5 bytes: stream = byte*8+bit,
//     col = 5-(stream%5), row = 7-(stream/5); 5 trailing bits unused.
//   - Button status: interrupt line pulls low, then host reads 4 bytes
//     from 0x70. Byte layout: [0x02][mask1][mask2][state], state 0x00
//     press / 0xFF release. mask1: right=0x01 down=0x08 left=0x20 ok=0x40
//     up=0x80. mask2: power=0x01.
//   - IR remote status: same interrupt line, same 4-byte read shape, but
//     tagged with 0x01 instead of 0x02. Byte layout: [0x01][code][state]
//     [0x08], state 0x00 press / 0xFF release (0x08 in the 4th byte is
//     always present, purpose unclear). "code" identifies which remote
//     button -- see IrButton for the full keymap, tested by hand against
//     real button presses (via this library, not the original SoC).
//     The chip appears to fully decode whatever IR protocol/remote it's
//     paired with on-chip -- a different remote produced no I2C traffic
//     at all, so raw IR timing is not accessible through this interface.
//   - A secondary address (0x4A in the original capture) exists on the
//     original mainboard but is NOT present on the standalone device
//     board -- excluded from this library.
//
// LEDs: independently controllable color (blue/red) via setLedColor() --
// same command family as display power (ED 05 ..) but a distinct byte,
// and does not itself affect brightness or displayed content.
// ---------------------------------------------------------------------------

class VfdController {
public:
  static constexpr uint8_t I2C_ADDR = 0x70;
  static constexpr size_t FRAME_PAYLOAD_LEN = 81;
  static constexpr size_t FRAME_TOTAL_LEN = 1 + FRAME_PAYLOAD_LEN; // + cmd byte
  static constexpr uint8_t FRAME_CMD_BYTE = 0x10;
  static constexpr size_t NUM_SEGMENTS = 16;
  static constexpr size_t SEG_COLS = 5;
  static constexpr size_t SEG_ROWS = 7;
  static constexpr size_t BUTTON_READ_LEN = 4;

  struct ButtonState {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool ok = false;
    bool power = false;
    bool pressed = false; // true=press event, false=release event
  };

  // Known IR remote button codes (byte1 of the 01/0C-style report), all
  // tested by pressing each physical remote button and checking the
  // decoded code against this library's own reads (not the original SoC).
  enum class IrButton : uint8_t {
    NUM_0 = 0x00,
    NUM_1 = 0x01,
    NUM_2 = 0x02,
    NUM_3 = 0x03,
    NUM_4 = 0x04,
    NUM_5 = 0x05,
    NUM_6 = 0x06,
    NUM_7 = 0x07,
    NUM_8 = 0x08,
    NUM_9 = 0x09,
    OPT = 0x0A,
    POWER = 0x0C,
    MUTE = 0x0D,
    VOLUME_UP = 0x10,
    VOLUME_DOWN = 0x11,
    PROGRAM_UP = 0x20,
    PROGRAM_DOWN = 0x21,
    BACK = 0x22,   // labeled "EXIT" on a second remote seen for this device
    AUDIO = 0x23,
    STILL = 0x29,
    SLEEP = 0x26,
    SFI = 0x2F,
    REVERSE = 0x32,
    FORWARD = 0x34,
    PLAY_PAUSE = 0x35,
    STOP = 0x36,
    RECORD = 0x37,
    TXT = 0x3C,
    GENRE = 0x78,  // seen on a second remote for this device, not the first
    INFO = 0x4F,
    UP = 0x50,
    DOWN = 0x51,
    MENU = 0x52,
    TV_RADIO = 0x53,
    LEFT = 0x55,
    RIGHT = 0x56,
    OK = 0x57,
    PIP = 0x58,
    NAV = 0x69,
    TIMER = 0x6A,
    RED = 0x6B,
    GREEN = 0x6C,
    YELLOW = 0x6D,
    BLUE = 0x6E,
    PAGE_UP = 0x74,
    PAGE_DOWN = 0x75,
    HELP = 0x7D,   // seen on a second remote for this device, not the first
    HDMI = 0x79,
    ZOOM = 0x7E,
    WWW = 0x7F,    // labeled "MORE" on a second remote seen for this device
    OTHER = 0xFF,  // any code not in this enum
  };
  // NOTE on 0x3F: seen while cycling the remote's "code"/protocol-select
  // button (hold code + press OK to cycle between 3 protocol modes; only
  // one of the 3 is actually understood by this receiver chip). 0x3F is
  // very likely some kind of protocol identifier or handshake message
  // rather than a real button press, so it's deliberately NOT in
  // IrButton -- if you see raw code 0x3F come through, treat it as
  // "receiver saw something, probably a mode-select artifact", not as a
  // button the person pressed.

  struct IrEvent {
    IrButton button;
    uint8_t rawCode; // the raw byte1 value; always valid, even for OTHER
    bool pressed;    // true=press event, false=release event
  };

  // Callback signature for button events. Called from the main loop context
  // (via poll()), not from an ISR, so it's safe to do I2C/Serial/etc in it.
  using ButtonCallback = void (*)(const ButtonState &state);

  // Callback signature for IR remote events. Same calling context as
  // ButtonCallback (from poll(), not an ISR).
  using IrCallback = void (*)(const IrEvent &event);

  // sdaPin/sclPin: I2C pins to use. intPin: GPIO wired to the device's
  // interrupt/data-ready line (active low). wire: which TwoWire instance
  // to use (default Wire).
  //
  // NOTE: INT has been observed behaving unreliably in practice (misses
  // events intermittently, sometimes recovers after an ESP32 reset).
  // Root cause not yet confirmed -- current suspicion is device-side
  // state (e.g. having been detached from the original SoC mid-state, or
  // something left over from toggling display power via ED 05 13/23)
  // rather than a pull-up/signal integrity issue. If you hit this,
  // power-cycling the device (not just the ESP32) is worth trying.
  VfdController(int sdaPin, int sclPin, int intPin, TwoWire &wire = Wire);

  // Call once in setup(). Configures pins and I2C, but does NOT run the
  // init sequence -- call initDevice() separately (kept apart so you can
  // control exactly when it happens, e.g. after a power rail stabilizes).
  void begin(uint32_t i2cClockHz = 100000);

  // Sends the minimal confirmed init sequence (3 commands) needed for
  // button and IR reporting to work. NOT needed for VFD display frames,
  // which work fine without calling this at all -- but call it anyway
  // if you want buttons/IR, since it's harmless either way.
  // Returns true if every command was ACKed.
  bool initDevice();

  // Call frequently from loop(). Services the interrupt flag; if a button
  // event is pending, reads it and invokes the registered callback (if
  // any). Safe/cheap to call every loop iteration.
  void poll();

  // Register a callback for button press/release events. Pass nullptr to
  // clear.
  void onButtonEvent(ButtonCallback cb);

  // Register a callback for IR remote press/release events. Pass nullptr
  // to clear. See IrButton for the full remote keymap.
  void onIrEvent(IrCallback cb);

  // Human-readable name for a button, e.g. "OK", "VOLUME_UP", "OTHER".
  // Handy for logging/debugging.
  static const char *irButtonName(IrButton button);

  // Low-level: send a raw pre-built 81-byte payload (segments+header,
  // NOT including the 0x10 command byte). Returns the I2C
  // endTransmission() result code (0 = success).
  uint8_t sendRawPayload(const uint8_t *payload81);

  // Low-level: send an arbitrary short (5-byte) command, same shape as
  // the init sequence and the VFD brightness commands (0xE7/0xE8/0xEA/
  // 0xEB/0xED all use this 5-byte format: [cmd][b1][b2][b3][b4]). Useful
  // for probing not-yet-understood commands (e.g. LED control) the same
  // way the VFD segment layout was mapped -- see VfdController.h's TODOs.
  // Returns the I2C endTransmission() result code (0 = success).
  uint8_t sendRawCommand(uint8_t cmd, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4);

  // Set VFD brightness, 0 (dimmest) to 7 (brightest), confirmed against
  // real hardware as 8 distinct, stable levels. Values outside 0-7 are
  // clamped. Returns the I2C endTransmission() result code.
  uint8_t setBrightness(uint8_t level);

  // LED color, confirmed against real hardware by capturing the original
  // SoC's Power-button handling and correlating each command with the
  // LED color observed at that moment. This is independent of both
  // display power (see setDisplayPower()) and brightness (see
  // setBrightness()) -- it only changes the LED, does not itself dim,
  // blank, or clear the display content.
  enum class LedColor {
    BLUE, // ED 05 15 00 00
    RED,  // ED 05 13 00 00
  };

  // Sets LED color only. Returns the I2C endTransmission() result code.
  uint8_t setLedColor(LedColor color);

  // Last LED color set via setLedColor() or setDisplayPower(). Tracked
  // in software (there's no known "read LED state" command), so this is
  // only accurate as long as nothing else changes it behind this
  // library's back. Defaults to BLUE, matching the device's power-on
  // default.
  LedColor ledColor() const { return _ledColor; }

  // Display power on/off. OFF cuts the VFD's HV supply (~0.011A vs
  // ~0.2A) and clears displayed content -- turning back ON does not
  // restore what was showing before, you'll need to send a fresh frame
  // (printText/sendSegments/clear). The original SoC re-sends the OFF
  // command every ~1s while off (looks like a keepalive/watchdog ping)
  // -- if the device seems to wake on its own after a while, try
  // re-sending setDisplayPower(false) periodically yourself.
  // NOTE: waking from OFF in the original capture also involved an
  // `ED 10 01 00 00` write right before the fresh frame write -- if
  // turning power back on doesn't fully restore the display, that's
  // worth trying too (not yet wrapped in a named method).
  // NOTE: turning power back ON also resets LED color to BLUE (matches
  // observed device behavior -- see setDisplayPower()'s implementation).
  uint8_t setDisplayPower(bool on);

  // Last display power state set via setDisplayPower(). Tracked in
  // software (there's no known "read power state" command), so this is
  // only accurate as long as nothing else changes it behind this
  // library's back -- e.g. if the device has a physical power switch
  // separate from I2C, this won't see that. Defaults to true (on),
  // matching the assumption that the device is powered/awake when your
  // sketch starts.
  bool displayPower() const { return _displayPower; }

  // Manually reads whatever 4-byte report is currently available from
  // the device, completely independent of the interrupt line/flag --
  // no waiting for INT, no discard-on-unrecognized-type logic, just a
  // raw on-demand read. Useful for debugging INT reliability: compare
  // what this returns against intPinLevel() to see whether the device
  // has data ready even when INT isn't (or is) asserted.
  // Returns true if 4 bytes were successfully read into out[4].
  bool readRaw(uint8_t out[4]);

  // Current raw level of the interrupt pin (HIGH or LOW), read directly,
  // bypassing the ISR/flag entirely. Useful alongside readRaw() for
  // debugging INT behavior.
  int intPinLevel() const { return digitalRead(_intPin); }

  // Higher-level: build a frame from up to 16 segment grids and send it.
  // Each grid is SEG_ROWS x SEG_COLS (7x5) booleans, row-major, row0=top,
  // as physically displayed (this class handles whatever internal flip
  // is needed to match the confirmed hardware bit order).
  uint8_t sendSegments(const uint8_t grids[][SEG_ROWS][SEG_COLS], size_t numGrids);

  // Convenience: render text across up to 16 segments using the built-in
  // 5x7 font, UTF-8 decoded (so German umlauts/ß can be written directly
  // in source as e.g. "Schlüssel"). Left-aligned, starting at segment 0.
  // Unsupported characters render blank. Text longer than 16 characters
  // is truncated -- see printTextCentered() and scrolling support below
  // for longer text. Returns the I2C endTransmission() result code.
  uint8_t printText(const char *text);

  // Like printText(), but centers the text across the 16 segments (extra
  // padding segment, if the length is odd, goes on the right). Text
  // longer than 16 characters is truncated, same as printText().
  uint8_t printTextCentered(const char *text);

  // Clears the display (sends an all-blank frame).
  uint8_t clear();

  // --- Scrolling text ------------------------------------------------
  // Non-blocking marquee: call startScroll() once, then call update()
  // regularly from loop() (alongside poll()) -- it sends a new frame
  // whenever intervalMs has elapsed since the last one, and does nothing
  // otherwise, so it's cheap to call every iteration. Any printText/
  // printTextCentered/sendSegments/clear call cancels an active scroll.
  //
  // The message scrolls right-to-left, one whole character (segment) at
  // a time -- not smooth sub-segment movement. It wraps around seamlessly
  // with `gapSegments` blank segments' worth of gap between the end and
  // the repeated start.
  void startScroll(const char *text, uint32_t intervalMs = 150, uint8_t gapSegments = 2);
  void stopScroll();
  bool isScrolling() const { return _scrolling; }

  // Call every loop() iteration (like poll()). No-op if not scrolling or
  // if not enough time has passed since the last scroll step.
  void update();

  // When true, poll() logs unrecognized report types (byte0 not 0x01 or
  // 0x02) to Serial -- useful when probing for not-yet-decoded report
  // formats. Off by default; uses Serial.printf (not bare printf, which
  // has no stdout target on Arduino-ESP32 and can crash).
  void setDebug(bool enabled) { _debugEnabled = enabled; }
  bool debugEnabled() const { return _debugEnabled; }

  // Everything originally listed as a TODO here (LED control, remaining
  // init commands) is now resolved -- see setPowerState() for LEDs, and
  // initDevice()/VfdController.cpp for the full picture on init commands.

private:
  int _sdaPin;
  int _sclPin;
  int _intPin;
  TwoWire &_wire;
  ButtonCallback _buttonCb = nullptr;
  IrCallback _irCb = nullptr;
  bool _debugEnabled = false;
  LedColor _ledColor = LedColor::BLUE;
  bool _displayPower = true;

  static IrButton irCodeToButton(uint8_t rawCode);

  static void IRAM_ATTR isrTrampoline();
  static VfdController *_isrInstance; // supports one active instance for the ISR
  volatile bool _dataReadyFlag = false;

  void encodeSegment(const uint8_t grid[SEG_ROWS][SEG_COLS], uint8_t out5[5]);

  // Internal: sends a frame without touching _scrolling (used by the
  // scroll renderer itself, which must NOT cancel scrolling on its own
  // frame updates). Public sendSegments() wraps this and clears
  // _scrolling first.
  uint8_t sendSegmentsInternal(const uint8_t grids[][SEG_ROWS][SEG_COLS], size_t numGrids);

  // Builds up to numChars glyphs (UTF-8 decoded) into out, returns how
  // many glyphs were produced (<= NUM_SEGMENTS).
  size_t decodeToGlyphs(const char *text, uint8_t out[][SEG_ROWS][SEG_COLS], size_t maxGlyphs);

  // --- Scrolling state -------------------------------------------------
  static constexpr size_t SCROLL_MAX_CHARS = 64; // plenty for a status line
  uint8_t _scrollGlyphs[SCROLL_MAX_CHARS][SEG_ROWS][SEG_COLS];
  size_t _scrollNumGlyphs = 0;
  uint8_t _scrollGap = 2;
  uint32_t _scrollIntervalMs = 150;
  uint32_t _scrollLastStepMs = 0;
  size_t _scrollSegOffset = 0; // current scroll position, in whole segments
  bool _scrolling = false;

  // Renders _scrollSegOffset into a 16-segment frame and sends it.
  void renderScrollFrame();
};