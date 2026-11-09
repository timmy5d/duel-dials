#include "input.h"

#include <Wire.h>

#include "board.h"

namespace {

constexpr uint8_t TP_ADDR = 0x15;
constexpr uint32_t LONG_PRESS_MS = 600;
constexpr uint32_t DOUBLE_CLICK_MS = 300;
constexpr uint32_t DEBOUNCE_MS = 15;
constexpr uint32_t TOUCH_POLL_MS = 15;
constexpr uint32_t TAP_MAX_MS = 450;
constexpr int TOUCH_SLOP = 18;

// Quadrature decode table, A = bit1, B = bit0 (from the Elecrow reference).
DRAM_ATTR const int8_t kEncTable[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

portMUX_TYPE encMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint8_t encLast = 0;
volatile int8_t encQuarter = 0;
volatile int32_t encDetents = 0;

bool dblEnabled = false;
bool reversed = false;

// Button state
bool btnStable = false;  // true = pressed
bool btnRaw = false;
uint32_t btnRawSince = 0;
uint32_t btnDownAt = 0;
bool btnLongFired = false;
bool btnResetFired = false;
uint8_t pendingClicks = 0;
uint32_t lastReleaseAt = 0;
uint8_t rapidClicks = 0;  // presses in quick succession (for the triple press)

// Touch state
bool touchDown = false;
uint32_t touchDownAt = 0;
int16_t touchStartX = 0, touchStartY = 0, touchLastX = 0, touchLastY = 0;
bool touchMoved = false;
bool touchHoldFired = false;
uint32_t lastTouchPoll = 0;

void IRAM_ATTR encoderISR() {
  uint8_t s = ((uint8_t)digitalRead(PIN_ENC_A) << 1) | (uint8_t)digitalRead(PIN_ENC_B);
  portENTER_CRITICAL_ISR(&encMux);
  int8_t m = kEncTable[(encLast << 2) | s];
  encLast = s;
  if (m) {
    encQuarter += m;
    if (encQuarter >= 4) {
      encDetents++;
      encQuarter -= 4;
    } else if (encQuarter <= -4) {
      encDetents--;
      encQuarter += 4;
    }
  }
  portEXIT_CRITICAL_ISR(&encMux);
}

void tpWrite(uint8_t reg, uint8_t val) {
  Wire1.beginTransmission(TP_ADDR);
  Wire1.write(reg);
  Wire1.write(val);
  Wire1.endTransmission();
}

// Reads finger count and first touch point. Returns false on I2C error.
bool tpRead(bool &down, int16_t &x, int16_t &y) {
  Wire1.beginTransmission(TP_ADDR);
  Wire1.write(0x02);
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom((int)TP_ADDR, 5) != 5) return false;
  uint8_t d[5];
  for (auto &b : d) b = Wire1.read();
  down = (d[0] & 0x0F) > 0;
  x = ((d[1] & 0x0F) << 8) | d[2];
  y = ((d[3] & 0x0F) << 8) | d[4];
  return true;
}

}  // namespace

namespace input {

void begin() {
  pinMode(PIN_ENC_A, INPUT);
  pinMode(PIN_ENC_B, INPUT);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  encLast = ((uint8_t)digitalRead(PIN_ENC_A) << 1) | (uint8_t)digitalRead(PIN_ENC_B);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encoderISR, CHANGE);

  Wire1.begin(PIN_TP_SDA, PIN_TP_SCL, 400000);
  pinMode(PIN_TP_INT, OUTPUT);
  digitalWrite(PIN_TP_INT, HIGH);
  delay(1);
  digitalWrite(PIN_TP_INT, LOW);
  delay(1);
  pinMode(PIN_TP_RST, OUTPUT);
  digitalWrite(PIN_TP_RST, LOW);
  delay(10);
  digitalWrite(PIN_TP_RST, HIGH);
  delay(300);
  tpWrite(0xFE, 0xFF);  // disable auto-sleep so polling always answers
}

void setDoubleClickEnabled(bool on) { dblEnabled = on; }
void setReversed(bool on) { reversed = on; }
bool active() { return touchDown || btnStable; }

InputEvents poll() {
  InputEvents ev;
  uint32_t now = millis();

  // Encoder
  portENTER_CRITICAL(&encMux);
  int32_t d = encDetents;
  encDetents = 0;
  portEXIT_CRITICAL(&encMux);
  ev.detents = reversed ? -d : d;

  // Button (debounced)
  bool raw = digitalRead(PIN_ENC_SW) == LOW;
  if (raw != btnRaw) {
    btnRaw = raw;
    btnRawSince = now;
  }
  if (btnRaw != btnStable && now - btnRawSince >= DEBOUNCE_MS) {
    btnStable = btnRaw;
    if (btnStable) {
      btnDownAt = now;
      btnLongFired = false;
      btnResetFired = false;
    } else if (!btnLongFired) {
      rapidClicks = now - lastReleaseAt <= DOUBLE_CLICK_MS ? rapidClicks + 1 : 1;
      lastReleaseAt = now;
      if (dblEnabled) {
        pendingClicks++;
        if (pendingClicks >= 3) {  // no need to wait: a triple press is final
          ev.tripleClick = true;
          pendingClicks = rapidClicks = 0;
        }
      } else {
        ev.click = true;
        if (rapidClicks >= 3) {
          ev.tripleClick = true;
          rapidClicks = 0;
        }
      }
    }
  }
  if (btnStable && !btnLongFired && now - btnDownAt >= LONG_PRESS_MS) {
    btnLongFired = true;
    pendingClicks = 0;
    ev.longPress = true;
  }
  if (btnStable && !btnResetFired) {  // screen pressed in and held: countdown, then reset
    uint32_t held = now - btnDownAt;
    if (held >= RESET_HOLD_MS) {
      btnResetFired = true;
      ev.holdReset = true;
    } else if (held >= BTN_HOLD_SHOW_MS) {
      ev.btnHoldMs = held;
    }
  }
  // With multi-press detection on, wait out the window so a double press can still become a triple.
  if (pendingClicks && !btnStable && now - lastReleaseAt > DOUBLE_CLICK_MS) {
    if (pendingClicks >= 2) ev.doubleClick = true;
    else ev.click = true;
    pendingClicks = 0;
  }

  // Touch
  if (now - lastTouchPoll >= TOUCH_POLL_MS) {
    lastTouchPoll = now;
    bool down;
    int16_t x, y;
    if (tpRead(down, x, y)) {
      if (down && x < SCREEN_W && y < SCREEN_H) {
        if (!touchDown) {
          touchDown = true;
          touchDownAt = now;
          touchStartX = x;
          touchStartY = y;
          touchMoved = false;
          touchHoldFired = false;
        }
        touchLastX = x;
        touchLastY = y;
        if (abs(x - touchStartX) > TOUCH_SLOP || abs(y - touchStartY) > TOUCH_SLOP) touchMoved = true;
        if (touchMoved && !touchHoldFired) {
          ev.dragging = true;
          ev.sx = touchStartX;
          ev.sy = touchStartY;
          ev.x = x;
          ev.y = y;
        }
        uint32_t held = now - touchDownAt;
        if (!touchMoved && held >= HOLD_MS) {
          touchHoldFired = true;
          ev.x = touchStartX;
          ev.y = touchStartY;
          ev.holding = true;
          ev.holdMs = held;
        }
      } else if (!down && touchDown) {
        touchDown = false;
        if (touchMoved && !touchHoldFired) {
          ev.dragEnd = true;
          ev.sx = touchStartX;
          ev.sy = touchStartY;
          ev.x = touchLastX;
          ev.y = touchLastY;
        }
        if (touchHoldFired && !touchMoved) {
          ev.holdRelease = true;
          ev.holdMs = now - touchDownAt;
          ev.x = touchStartX;
          ev.y = touchStartY;
        }
        if (!touchMoved && !touchHoldFired && now - touchDownAt <= TAP_MAX_MS) {
          ev.tap = true;
          ev.x = touchStartX;
          ev.y = touchStartY;
        }
      }
    }
  }
  return ev;
}

}  // namespace input
