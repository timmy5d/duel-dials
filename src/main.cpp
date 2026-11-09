// MTG Dial - a Magic: The Gathering life and damage tracker for the
// Elecrow CrowPanel 1.28" ESP32-S3 rotary display.
#include <Arduino.h>

#include "board.h"
#include "game.h"
#include "input.h"
#include "net.h"
#include "ui.h"
#include "web.h"

LGFX tft;

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  delay(200);
  Serial.println("MTG Dial boot");

  pinMode(PIN_POWER_LED, OUTPUT);
  digitalWrite(PIN_POWER_LED, LOW);
  pinMode(PIN_PWR_EN_1, OUTPUT);
  digitalWrite(PIN_PWR_EN_1, HIGH);
  pinMode(PIN_PWR_EN_2, OUTPUT);
  digitalWrite(PIN_PWR_EN_2, HIGH);

  mtg::load();
  input::begin();

  tft.init();
  tft.fillScreen(TFT_BLACK);

  ui::begin(tft);
  net::begin();
  web::begin();
  Serial.println("MTG Dial ready");
}

// Serial debug console: lets a host script inject input and grab screenshots.
//   r / l        dial one detent clockwise / counter-clockwise (r5 = five)
//   c / d / L    click / double-click / long-press the dial button
//   t X Y        tap at X,Y      h X Y   touch-and-hold at X,Y
//   s            dump the frame as raw RGB565 (prefixed by "SHOT 240 240\n")
static uint32_t simPressAt = 0, simPressMs = 0;  // simulated press-in of the screen
static bool simLong = false, simReset = false;

static void debugConsole(InputEvents &ev) {
  if (simPressMs) {  // replay what input::poll reports while the button is held
    uint32_t held = millis() - simPressAt;
    if (held >= simPressMs) {
      simPressMs = 0;
    } else if (!simReset) {
      if (held >= 600 && !simLong) simLong = ev.longPress = true;
      if (held >= RESET_HOLD_MS) simReset = ev.holdReset = true;
      else if (held >= BTN_HOLD_SHOW_MS) ev.btnHoldMs = held;
    }
  }
  static char line[32];
  static int len = 0;
  static bool pendingDragEnd = false;
  static int16_t pd[4];
  if (pendingDragEnd) {
    pendingDragEnd = false;
    ev.dragEnd = true;
    ev.sx = pd[0], ev.sy = pd[1], ev.x = pd[2], ev.y = pd[3];
    return;
  }
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch != '\n' && ch != '\r') {
      if (len < (int)sizeof(line) - 1) line[len++] = ch;
      continue;
    }
    line[len] = 0;
    if (!len) continue;
    len = 0;
    int a = 0, b = 0;
    switch (line[0]) {
      case 'r': ev.detents += line[1] ? atoi(line + 1) : 1; break;
      case 'l': ev.detents -= line[1] ? atoi(line + 1) : 1; break;
      case 'c': ev.click = true; break;
      case 'd': ev.doubleClick = true; break;
      case 'T': ev.tripleClick = true; break;
      case 'J': web::debugDump(); break;
      case 'R': {  // R ROOM SEAT: link room (0 = off) and this dial's seat (0 = none)
        int room = 0, seat = 0;
        sscanf(line + 1, "%d %d", &room, &seat);
        settings.linkRoom = constrain(room, 0, 9);
        settings.mySeat = constrain(seat, 0, MAX_PLAYERS);
        mtg::markSettingsDirty();
        net::setRoom(settings.linkRoom);
        Serial.printf("room %d seat %d\n", settings.linkRoom, settings.mySeat);
        break;
      }
      case 'L': ev.longPress = true; break;
      case 'g': ui::showGallery(); break;
      case 'n': net::debugPrint(); break;
      case 'm':  // m SX SY EX EY: drag and release;  M ...: drag and keep holding
      case 'M': {
        int v[4];
        if (sscanf(line + 1, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]) == 4) {
          ev.dragging = true;
          ev.sx = v[0], ev.sy = v[1], ev.x = v[2], ev.y = v[3];
          if (line[0] == 'm') {
            for (int i = 0; i < 4; i++) pd[i] = v[i];
            pendingDragEnd = true;
          }
        }
        break;
      }
      case 't':
      case 'h':  // h X Y: touch-and-hold then release (opens the menu)
        if (sscanf(line + 1, "%d %d", &a, &b) == 2) {
          (line[0] == 't' ? ev.tap : ev.holdRelease) = true;
          ev.holdMs = 1000;
          ev.x = a;
          ev.y = b;
        }
        break;
      case 'P': {  // P MS [X Y]: finger still held for MS milliseconds (at X,Y)
        int ms = 0, px = 120, py = 120;
        sscanf(line + 1, "%d %d %d", &ms, &px, &py);
        ev.holding = true;
        ev.holdMs = ms;
        ev.x = px;
        ev.y = py;
        break;
      }
      case 'H':  // screen pressed in for 3 s: reset the game
        ev.holdReset = true;
        break;
      case 'B': {  // B MS: press the screen in and keep it held for MS milliseconds
        int ms = 0;
        sscanf(line + 1, "%d", &ms);
        simPressAt = millis();
        simPressMs = ms;
        simLong = simReset = false;
        break;
      }
      case 's': {
        LGFX_Sprite &f = ui::frame();
        Serial.setTxTimeoutMs(500);
        Serial.printf("SHOT %d %d\n", f.width(), f.height());
        const uint8_t *p = (const uint8_t *)f.getBuffer();
        size_t n = f.width() * f.height() * 2;
        for (size_t off = 0; off < n; off += 4096) Serial.write(p + off, min((size_t)4096, n - off));
        Serial.flush();
        Serial.setTxTimeoutMs(0);
        break;
      }
    }
  }
}

void loop() {
  uint32_t now = millis();
  InputEvents ev = input::poll();
  debugConsole(ev);
  net::loop(now);
  web::loop(now);
  ui::handle(ev, now);
  ui::update(now);
  delay(2);
}
