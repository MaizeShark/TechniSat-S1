// Auto-generated replay sketch.
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
                    "increase Wire.setBufferSize() in setup(), this write will be truncated.
",
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

  Serial.printf("Replaying %d I2C transactions...\n", I2C_TXN_COUNT);

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

    Serial.printf("[%d] addr=0x%02X %s len=%d\n", i, txn.address,
                  txn.is_read ? "READ" : "WRITE", txn.len);

    replayTransaction(txn);
  }

  Serial.println("Replay complete.");
}

void loop() {
  // Replay runs once in setup(). Add looping/repeat logic here if desired.
}
