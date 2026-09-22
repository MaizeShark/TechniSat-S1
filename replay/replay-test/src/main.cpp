#include <Arduino.h>
#include <VfdController.h>
#include <string.h>

// --- Wiring: adjust to match your board ---
#define SDA_PIN 21
#define SCL_PIN 22
#define INT_PIN 4

VfdController vfd(SDA_PIN, SCL_PIN, INT_PIN);

void onButton(const VfdController::ButtonState &s) {
  const char *verb = s.pressed ? "pressed" : "released";
  if (s.up)    Serial.printf("UP %s\n", verb);
  if (s.down)  Serial.printf("DOWN %s\n", verb);
  if (s.left)  {
    Serial.printf("LEFT %s\n", verb);
    vfd.setLedColor(VfdController::LedColor::RED);
  }
  if (s.right) {
    Serial.printf("RIGHT %s\n", verb);
    vfd.setLedColor(VfdController::LedColor::BLUE);
  }
  if (s.ok)    {
    Serial.printf("OK %s\n", verb);
    vfd.printTextCentered("MaizeShark");
  }
  if (s.power) {
    Serial.printf("POWER %s\n", verb);
    if (s.pressed) {
      vfd.setDisplayPower(!vfd.displayPower());
      if (vfd.displayPower()) vfd.printText("HELLO"); // OFF clears content, so restore something on wake
    }
  }
}

void onIr(const VfdController::IrEvent &e) {
  const char *verb = e.pressed ? "pressed" : "released";
  const char *name = VfdController::irButtonName(e.button); // resolve first, don't nest in printf
  Serial.printf("IR %s %s (raw code 0x%02X)\n", name, verb, e.rawCode);

  if (e.button == VfdController::IrButton::POWER && e.pressed) {
    vfd.setDisplayPower(!vfd.displayPower());
    if (vfd.displayPower()) vfd.printText("HELLO");
  }
}

void handleLine(const String &line) {
  if (line.length() == 0) return;

  if (line == "r") {
    // Manual raw read, independent of INT -- for debugging INT
    // reliability: shows what's actually available right now, and what
    // level the INT pin is at, regardless of whether an edge fired.
    int level = vfd.intPinLevel();
    uint8_t buf[4];
    bool ok = vfd.readRaw(buf);
    if (ok) {
      Serial.printf("INT=%s  raw read: %02X %02X %02X %02X\n",
                    level == LOW ? "LOW" : "HIGH", buf[0], buf[1], buf[2], buf[3]);
    } else {
      Serial.printf("INT=%s  raw read FAILED (no data / NACK)\n", level == LOW ? "LOW" : "HIGH");
    }
    return;
  }

  if (line == "ledblue" || line == "ledred") {
    VfdController::LedColor color = (line == "ledblue") ? VfdController::LedColor::BLUE : VfdController::LedColor::RED;
    uint8_t result = vfd.setLedColor(color);
    Serial.printf("setLedColor(%s) -> %s\n", line == "ledblue" ? "BLUE" : "RED", result == 0 ? "OK" : "FAIL");
    return;
  }

  if (line == "pwon" || line == "pwoff") {
    uint8_t result = vfd.setDisplayPower(line == "pwon");
    Serial.printf("setDisplayPower(%s) -> %s\n", line == "pwon" ? "true" : "false", result == 0 ? "OK" : "FAIL");
    return;
  }

  if (line.startsWith("br") && line.length() > 2) {
    int level = line.substring(2).toInt();
    uint8_t result = vfd.setBrightness((uint8_t)level);
    Serial.printf("setBrightness(%d) -> %s\n", level, result == 0 ? "OK" : "FAIL");
    return;
  }

  if (line[0] == 'c' && line.length() > 1 && line[1] == ' ') {
    // "c <cmd> <b1> <b2> <b3> <b4>" in hex, e.g. "c E7 30 2A 38 80"
    uint8_t vals[5];
    int idx = 0;
    int pos = 2;
    while (idx < 5 && pos < (int)line.length()) {
      while (pos < (int)line.length() && line[pos] == ' ') pos++; // skip spaces
      int start = pos;
      while (pos < (int)line.length() && line[pos] != ' ') pos++;
      if (pos > start) {
        vals[idx] = (uint8_t)strtol(line.substring(start, pos).c_str(), nullptr, 16);
        idx++;
      }
    }
    if (idx == 5) {
      uint8_t result = vfd.sendRawCommand(vals[0], vals[1], vals[2], vals[3], vals[4]);
      Serial.printf("sendRawCommand(%02X %02X %02X %02X %02X) -> %s\n",
                    vals[0], vals[1], vals[2], vals[3], vals[4],
                    result == 0 ? "OK" : "FAIL");
    } else {
      Serial.println("Usage: c <cmd> <b1> <b2> <b3> <b4>  (5 hex bytes), e.g. c E7 30 2A 38 80");
    }
    return;
  }

  if (line == "fill") {
    // Fill every segment fully lit -- quick visual sanity check.
    uint8_t litGrid[VfdController::SEG_ROWS][VfdController::SEG_COLS];
    for (auto &row : litGrid) for (auto &v : row) v = 1;
    uint8_t grids[VfdController::NUM_SEGMENTS][VfdController::SEG_ROWS][VfdController::SEG_COLS];
    for (auto &seg : grids) memcpy(seg, litGrid, sizeof(litGrid));
    uint8_t result = vfd.sendSegments(grids, VfdController::NUM_SEGMENTS);
    Serial.printf("fill -> %s\n", result == 0 ? "OK" : "FAIL");
    return;
  }

  if (line == "clear") {
    uint8_t result = vfd.clear();
    Serial.printf("clear -> %s\n", result == 0 ? "OK" : "FAIL");
    return;
  }

  if (line.startsWith("ct ")) {
    uint8_t result = vfd.printTextCentered(line.c_str() + 3);
    Serial.printf("printTextCentered -> %s\n", result == 0 ? "OK" : "FAIL");
    return;
  }

  if (line.startsWith("sc ")) {
    vfd.startScroll(line.c_str() + 3);
    Serial.println("startScroll -> started (call stopScroll()/send anything else to cancel)");
    return;
  }

  vfd.printText(line.c_str());
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("VfdController basic demo");

  vfd.begin();
  vfd.onButtonEvent(onButton);
  vfd.onIrEvent(onIr);

  Serial.println("Initializing device...");
  if (vfd.initDevice()) {
    Serial.println("Init OK");
  } else {
    Serial.println("Init FAILED (check wiring/address)");
  }

  vfd.printText("HELLO");
  vfd.setBrightness(7); // full brightness; try 0-7
  delay(2000);
  vfd.printTextCentered("HI");
  delay(2000);
  vfd.startScroll("This is a long scrolling message with ümlauts too!");

  Serial.println("Type text + Enter to display it, 'c <cmd> <b1> <b2> <b3> <b4>' (hex) for a raw command,");
  Serial.println("'br<0-7>' for brightness, 'ledblue'/'ledred' for LED color, 'pwon'/'pwoff' for display power,");
  Serial.println("'ct <text>' for centered text, 'sc <text>' to scroll, 'fill'/'clear', or 'r' to manually read+print raw status.");
  Serial.println("Power button (physical or IR) toggles display power automatically.");
}

void loop() {
  vfd.poll();  // services button events; call this often, it's cheap when idle
  vfd.update(); // advances scrolling text, if active; also cheap when idle

  // Type text into Serial Monitor + Enter to display it live, or a raw
  // command (see handleLine()) to probe not-yet-understood commands.
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (line.length() > 0) {
        handleLine(line);
        line = "";
      }
    } else {
      line += c;
    }
  }
}