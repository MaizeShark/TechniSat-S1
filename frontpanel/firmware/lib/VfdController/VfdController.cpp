#include "VfdController.h"
#include "VfdFont.h"
#include <string.h>

VfdController *VfdController::_isrInstance = nullptr;

VfdController::VfdController(int sdaPin, int sclPin, int intPin, TwoWire &wire)
    : _sdaPin(sdaPin), _sclPin(sclPin), _intPin(intPin), _wire(wire) {}

void VfdController::begin(uint32_t i2cClockHz) {
  pinMode(_intPin, INPUT_PULLUP);
  _isrInstance = this; // NOTE: single-instance limitation, see header
  attachInterrupt(digitalPinToInterrupt(_intPin), isrTrampoline, FALLING);
  _wire.begin(_sdaPin, _sclPin, i2cClockHz);
}

void IRAM_ATTR VfdController::isrTrampoline() {
  if (_isrInstance) {
    _isrInstance->_dataReadyFlag = true;
  }
}

bool VfdController::initDevice() {
  // Minimal confirmed init: only 3 of the original 11 captured commands
  // are actually required for button and IR reporting to work (verified
  // by testing each command individually against a working baseline).
  //   EA FF FF 88 01 + EA 0F 00 00 00  -> required for button reporting
  //   E8 08 0A 00 00                    -> required for IR reporting
  // Of the other 8 commands from the original capture, 2 are now fully
  // understood but not required, so not included here -- callers can
  // just call the named methods themselves if/when they want them:
  //   E7 A0 00 00 00  is just setBrightness(7)
  //   ED 05 15 00 00  is just setLedColor(BLUE) / setDisplayPower(true)
  // The remaining 6 (EE 06.., EB 14.., E8 08 0A 36 0D, ED 04..,
  //   E8 08 FF.., ED 10..) were each tested in isolation and made no
  //   observed difference to button/IR/VFD/LED behavior. Kept here,
  //   commented out, rather than deleted, in case they matter for
  //   something not yet tested.
  static const uint8_t initSeq[][5] = {
      {0xEA, 0xFF, 0xFF, 0x88, 0x01},
      {0xEA, 0x0F, 0x00, 0x00, 0x00},
      {0xE8, 0x08, 0x0A, 0x00, 0x00},
      // {0xEE, 0x06, 0x00, 0x00, 0x00},
      // {0xEB, 0x14, 0x0C, 0x00, 0x01},
      // {0xE8, 0x08, 0x0A, 0x36, 0x0D},
      // {0xED, 0x04, 0x23, 0x7F, 0x01},
      // {0xE8, 0x08, 0xFF, 0x00, 0x00},
      // {0xED, 0x10, 0x01, 0x00, 0x00},
  };
  constexpr size_t initSeqLen = sizeof(initSeq) / sizeof(initSeq[0]);

  bool allOk = true;
  for (size_t i = 0; i < initSeqLen; i++) {
    _wire.beginTransmission(I2C_ADDR);
    _wire.write(initSeq[i], 5);
    uint8_t result = _wire.endTransmission();
    if (result != 0) allOk = false;
    delay(20); // roughly matches the spacing seen in the original capture
  }

  // Before the button-enable commands take effect, reads return a
  // repeating, non-garbage-looking but unrecognized report (0x03 FA 00
  // 00) -- harmless (poll() discards unrecognized report types), cause
  // not yet understood. Also: interrupts firing during init should be
  // discarded so the first poll() afterwards doesn't act on stale state.
  _dataReadyFlag = false;

  return allOk;
}

bool VfdController::readRaw(uint8_t out[4]) {
  uint8_t got = _wire.requestFrom((uint8_t)I2C_ADDR, (uint8_t)BUTTON_READ_LEN);
  if (got != BUTTON_READ_LEN) {
    while (_wire.available()) _wire.read();
    return false;
  }
  for (size_t i = 0; i < BUTTON_READ_LEN; i++) out[i] = _wire.read();
  return true;
}

void VfdController::poll() {
  if (!_dataReadyFlag) return;
  _dataReadyFlag = false;

  uint8_t buf[BUTTON_READ_LEN];
  if (!readRaw(buf)) return;

  // byte0 tags the report type: 0x02 = physical button, 0x01 = IR remote.
  // Anything else is very likely a garbage read (e.g. bus noise or an
  // interrupt-line glitch during init) rather than a real event --
  // discard it instead of reporting bogus state.
  if (buf[0] == 0x02) {
    if (_buttonCb == nullptr) return;

    ButtonState s;
    s.pressed = (buf[3] == 0x00);
    s.right = (buf[1] & 0x01) != 0;
    s.down  = (buf[1] & 0x08) != 0;
    s.left  = (buf[1] & 0x20) != 0;
    s.ok    = (buf[1] & 0x40) != 0;
    s.up    = (buf[1] & 0x80) != 0;
    s.power = (buf[2] & 0x01) != 0;

    _buttonCb(s);
    return;
  }

  if (buf[0] == 0x01) {
    if (_irCb == nullptr) return;

    IrEvent e;
    e.rawCode = buf[1];
    e.button = irCodeToButton(buf[1]);
    e.pressed = (buf[2] == 0x00); // note: state is byte2 here, not byte3 like button reports

    _irCb(e);
    return;
  }

  // Unrecognized report type -- discard, optionally log for debugging.
  if (_debugEnabled) {
    Serial.printf("VfdController::poll(): unrecognized report type 0x%02X, discarding\n", buf[0]);
    Serial.printf("  raw bytes: %02X %02X %02X %02X\n", buf[0], buf[1], buf[2], buf[3]);
  }
}

void VfdController::onIrEvent(IrCallback cb) {
  _irCb = cb;
}

VfdController::IrButton VfdController::irCodeToButton(uint8_t rawCode) {
  switch (rawCode) {
    case 0x00: return IrButton::NUM_0;
    case 0x01: return IrButton::NUM_1;
    case 0x02: return IrButton::NUM_2;
    case 0x03: return IrButton::NUM_3;
    case 0x04: return IrButton::NUM_4;
    case 0x05: return IrButton::NUM_5;
    case 0x06: return IrButton::NUM_6;
    case 0x07: return IrButton::NUM_7;
    case 0x08: return IrButton::NUM_8;
    case 0x09: return IrButton::NUM_9;
    case 0x0A: return IrButton::OPT;
    case 0x0C: return IrButton::POWER;
    case 0x0D: return IrButton::MUTE;
    case 0x10: return IrButton::VOLUME_UP;
    case 0x11: return IrButton::VOLUME_DOWN;
    case 0x20: return IrButton::PROGRAM_UP;
    case 0x21: return IrButton::PROGRAM_DOWN;
    case 0x22: return IrButton::BACK;
    case 0x23: return IrButton::AUDIO;
    case 0x26: return IrButton::SLEEP;
    case 0x29: return IrButton::STILL;
    case 0x2F: return IrButton::SFI;
    case 0x32: return IrButton::REVERSE;
    case 0x34: return IrButton::FORWARD;
    case 0x35: return IrButton::PLAY_PAUSE;
    case 0x36: return IrButton::STOP;
    case 0x37: return IrButton::RECORD;
    case 0x3C: return IrButton::TXT;
    case 0x4F: return IrButton::INFO;
    case 0x50: return IrButton::UP;
    case 0x51: return IrButton::DOWN;
    case 0x52: return IrButton::MENU;
    case 0x53: return IrButton::TV_RADIO;
    case 0x55: return IrButton::LEFT;
    case 0x56: return IrButton::RIGHT;
    case 0x57: return IrButton::OK;
    case 0x58: return IrButton::PIP;
    case 0x69: return IrButton::NAV;
    case 0x6A: return IrButton::TIMER;
    case 0x6B: return IrButton::RED;
    case 0x6C: return IrButton::GREEN;
    case 0x6D: return IrButton::YELLOW;
    case 0x6E: return IrButton::BLUE;
    case 0x74: return IrButton::PAGE_UP;
    case 0x75: return IrButton::PAGE_DOWN;
    case 0x78: return IrButton::GENRE;
    case 0x79: return IrButton::HDMI;
    case 0x7D: return IrButton::HELP;
    case 0x7E: return IrButton::ZOOM;
    case 0x7F: return IrButton::WWW;
    default:   return IrButton::OTHER;
  }
}

const char *VfdController::irButtonName(IrButton button) {
  switch (button) {
    case IrButton::NUM_0: return "0";
    case IrButton::NUM_1: return "1";
    case IrButton::NUM_2: return "2";
    case IrButton::NUM_3: return "3";
    case IrButton::NUM_4: return "4";
    case IrButton::NUM_5: return "5";
    case IrButton::NUM_6: return "6";
    case IrButton::NUM_7: return "7";
    case IrButton::NUM_8: return "8";
    case IrButton::NUM_9: return "9";
    case IrButton::OPT: return "OPT";
    case IrButton::POWER: return "POWER";
    case IrButton::MUTE: return "MUTE";
    case IrButton::VOLUME_UP: return "VOLUME_UP";
    case IrButton::VOLUME_DOWN: return "VOLUME_DOWN";
    case IrButton::PROGRAM_UP: return "PROGRAM_UP";
    case IrButton::PROGRAM_DOWN: return "PROGRAM_DOWN";
    case IrButton::BACK: return "BACK";
    case IrButton::AUDIO: return "AUDIO";
    case IrButton::SLEEP: return "SLEEP";
    case IrButton::STILL: return "STILL";
    case IrButton::SFI: return "SFI";
    case IrButton::REVERSE: return "REVERSE";
    case IrButton::FORWARD: return "FORWARD";
    case IrButton::PLAY_PAUSE: return "PLAY_PAUSE";
    case IrButton::STOP: return "STOP";
    case IrButton::RECORD: return "RECORD";
    case IrButton::TXT: return "TXT";
    case IrButton::INFO: return "INFO";
    case IrButton::UP: return "UP";
    case IrButton::DOWN: return "DOWN";
    case IrButton::MENU: return "MENU";
    case IrButton::TV_RADIO: return "TV_RADIO";
    case IrButton::LEFT: return "LEFT";
    case IrButton::RIGHT: return "RIGHT";
    case IrButton::OK: return "OK";
    case IrButton::PIP: return "PIP";
    case IrButton::NAV: return "NAV";
    case IrButton::TIMER: return "TIMER";
    case IrButton::RED: return "RED";
    case IrButton::GREEN: return "GREEN";
    case IrButton::YELLOW: return "YELLOW";
    case IrButton::BLUE: return "BLUE";
    case IrButton::PAGE_UP: return "PAGE_UP";
    case IrButton::PAGE_DOWN: return "PAGE_DOWN";
    case IrButton::GENRE: return "GENRE";
    case IrButton::HDMI: return "HDMI";
    case IrButton::HELP: return "HELP";
    case IrButton::ZOOM: return "ZOOM";
    case IrButton::WWW: return "WWW";
    case IrButton::OTHER: default: return "OTHER";
  }
}

void VfdController::onButtonEvent(ButtonCallback cb) {
  _buttonCb = cb;
}

void VfdController::encodeSegment(const uint8_t grid[SEG_ROWS][SEG_COLS], uint8_t out5[5]) {
  // Confirmed hardware packing: stream = local_byte*8+bit, col=5-(stream%5),
  // row=7-(stream/5), 35 live bits then 5 dead padding bits.
  uint8_t bits[40] = {0};
  for (size_t p = 0; p < 35; p++) {
    uint8_t col = 5 - (p % 5);       // 5..1
    uint8_t row = 7 - (p / 5);       // 7..1
    bool on = grid[SEG_ROWS - row][col - 1] != 0;
    bits[p] = on ? 1 : 0;
  }
  for (size_t b = 0; b < 5; b++) {
    uint8_t val = 0;
    for (size_t bit = 0; bit < 8; bit++) {
      if (bits[b * 8 + bit]) val |= (1 << bit);
    }
    out5[b] = val;
  }
}

uint8_t VfdController::sendRawPayload(const uint8_t *payload81) {
  _wire.beginTransmission(I2C_ADDR);
  _wire.write(FRAME_CMD_BYTE);
  _wire.write(payload81, FRAME_PAYLOAD_LEN);
  return _wire.endTransmission();
}

uint8_t VfdController::sendRawCommand(uint8_t cmd, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4) {
  uint8_t buf[5] = {cmd, b1, b2, b3, b4};
  _wire.beginTransmission(I2C_ADDR);
  _wire.write(buf, 5);
  return _wire.endTransmission();
}

uint8_t VfdController::setBrightness(uint8_t level) {
  // Confirmed against real hardware: E7 <XX> 00 68 B3, XX in
  // {0x30,0x40,...,0xA0} for levels 0..7, 0x30=dimmest, 0xA0=brightest.
  // This matches the datasheet's 8-step dimmer function.
  if (level > 7) level = 7;
  uint8_t b1 = 0x30 + (level * 0x10);
  return sendRawCommand(0xE7, b1, 0x00, 0x68, 0xB3);
}

uint8_t VfdController::setLedColor(LedColor color) {
  uint8_t b2 = (color == LedColor::BLUE) ? 0x15 : 0x13;
  uint8_t result = sendRawCommand(0xED, 0x05, b2, 0x00, 0x00);
  if (result == 0) _ledColor = color;
  return result;
}

uint8_t VfdController::setDisplayPower(bool on) {
  // Confirmed: turning off is ED 05 23 00 00. Turning back on in the
  // original capture used ED 05 15 00 00 (i.e. the same command as
  // setLedColor(BLUE)) -- makes sense if "on" always implies blue.
  uint8_t b2 = on ? 0x15 : 0x23;
  uint8_t result = sendRawCommand(0xED, 0x05, b2, 0x00, 0x00);
  if (result == 0) {
    _displayPower = on;
    if (on) _ledColor = LedColor::BLUE; // matches observed device behavior
  }
  return result;
}

uint8_t VfdController::sendSegments(const uint8_t grids[][SEG_ROWS][SEG_COLS], size_t numGrids) {
  _scrolling = false; // any direct frame write cancels an active scroll
  return sendSegmentsInternal(grids, numGrids);
}

uint8_t VfdController::sendSegmentsInternal(const uint8_t grids[][SEG_ROWS][SEG_COLS], size_t numGrids) {
  if (numGrids > NUM_SEGMENTS) numGrids = NUM_SEGMENTS;

  uint8_t payload[FRAME_PAYLOAD_LEN];
  memset(payload, 0, sizeof(payload)); // payload[0] = reserved header byte, always 0x00

  static const uint8_t blank[SEG_ROWS][SEG_COLS] = {{0}};

  for (size_t s = 0; s < NUM_SEGMENTS; s++) {
    uint8_t segBytes[5];
    if (s < numGrids) {
      encodeSegment(grids[s], segBytes);
    } else {
      encodeSegment(blank, segBytes);
    }
    memcpy(&payload[1 + s * 5], segBytes, 5);
  }

  return sendRawPayload(payload);
}

size_t VfdController::decodeToGlyphs(const char *text, uint8_t out[][SEG_ROWS][SEG_COLS], size_t maxGlyphs) {
  size_t n = 0;
  const char *p = text;
  while (*p != '\0' && n < maxGlyphs) {
    uint32_t cp = vfdUtf8Decode(&p); // advances p past the consumed UTF-8 bytes
    vfdGetGlyph(cp, out[n]);
    n++;
  }
  return n;
}

uint8_t VfdController::printText(const char *text) {
  uint8_t grids[NUM_SEGMENTS][SEG_ROWS][SEG_COLS];
  size_t n = decodeToGlyphs(text, grids, NUM_SEGMENTS);
  return sendSegments(grids, n);
}

uint8_t VfdController::printTextCentered(const char *text) {
  uint8_t decoded[NUM_SEGMENTS][SEG_ROWS][SEG_COLS];
  size_t n = decodeToGlyphs(text, decoded, NUM_SEGMENTS);

  uint8_t grids[NUM_SEGMENTS][SEG_ROWS][SEG_COLS];
  memset(grids, 0, sizeof(grids)); // blank segments either side, by default

  size_t leftPad = (NUM_SEGMENTS - n) / 2; // extra odd segment ends up on the right
  for (size_t i = 0; i < n; i++) {
    memcpy(grids[leftPad + i], decoded[i], sizeof(decoded[i]));
  }

  return sendSegments(grids, NUM_SEGMENTS);
}

uint8_t VfdController::clear() {
  // numGrids=0 means every segment falls through to the "blank" case
  // inside sendSegments(), regardless of what's in the grids pointer, so
  // we can pass a minimal 1-element array here purely to satisfy the type.
  uint8_t dummy[1][SEG_ROWS][SEG_COLS] = {{{0}}};
  stopScroll();
  return sendSegments(dummy, 0);
}

// ---------------------------------------------------------------------------
// Scrolling text
//
// Character-by-character (whole-segment) scrolling: decode the message
// into a strip of glyphs once, then on each step advance by one glyph
// position and render a 16-segment window into that strip, wrapping
// around with `gapSegments` blank segments between the end and the
// repeated start. Each step moves a full segment at a time (not smooth
// sub-segment motion).
// ---------------------------------------------------------------------------

void VfdController::startScroll(const char *text, uint32_t intervalMs, uint8_t gapSegments) {
  _scrollNumGlyphs = decodeToGlyphs(text, _scrollGlyphs, SCROLL_MAX_CHARS);
  _scrollIntervalMs = intervalMs;
  _scrollGap = gapSegments;
  _scrollSegOffset = 0;
  _scrollLastStepMs = millis();
  _scrolling = (_scrollNumGlyphs > 0);
  if (_scrolling) renderScrollFrame();
}

void VfdController::stopScroll() {
  _scrolling = false;
}

void VfdController::update() {
  if (!_scrolling) return;
  uint32_t now = millis();
  if (now - _scrollLastStepMs < _scrollIntervalMs) return;
  _scrollLastStepMs = now;

  size_t totalSegs = _scrollNumGlyphs + _scrollGap;
  _scrollSegOffset = (_scrollSegOffset + 1) % totalSegs;
  renderScrollFrame();
}

void VfdController::renderScrollFrame() {
  size_t totalSegs = _scrollNumGlyphs + _scrollGap;

  uint8_t grids[NUM_SEGMENTS][SEG_ROWS][SEG_COLS];

  for (size_t seg = 0; seg < NUM_SEGMENTS; seg++) {
    size_t sourceSeg = (_scrollSegOffset + seg) % totalSegs;
    if (sourceSeg >= _scrollNumGlyphs) {
      memset(grids[seg], 0, sizeof(grids[seg])); // in the gap: blank segment
    } else {
      memcpy(grids[seg], _scrollGlyphs[sourceSeg], sizeof(grids[seg]));
    }
  }

  sendSegmentsInternal(grids, NUM_SEGMENTS);
}