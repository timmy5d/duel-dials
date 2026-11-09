// Rotary encoder, encoder push-button and CST816D touch input.
#pragma once
#include <Arduino.h>

struct InputEvents {
  int detents = 0;          // + clockwise, - counter-clockwise
  bool click = false;       // short press of the dial button
  bool doubleClick = false; // only reported when double-click detection is on
  bool tripleClick = false; // three quick presses of the screen (always reported)
  bool longPress = false;   // dial button held
  bool tap = false;         // short touch
  bool holding = false;     // finger held still past the hold threshold; holdMs = duration
  bool holdRelease = false; // finger lifted after a hold
  bool holdReset = false;   // screen pressed in (dial button) for RESET_HOLD_MS
  uint32_t btnHoldMs = 0;   // screen pressed in this long (from BTN_HOLD_SHOW_MS until the reset)
  uint32_t holdMs = 0;
  bool dragging = false;    // finger is moving; (sx, sy) = start, (x, y) = now
  bool dragEnd = false;     // finger lifted after a drag; (x, y) = release point
  int16_t x = 0, y = 0;     // coordinates of tap / hold / drag
  int16_t sx = 0, sy = 0;   // drag start

  bool any() const {
    return detents || click || doubleClick || tripleClick || longPress || tap || holding || holdRelease || holdReset || btnHoldMs ||
           dragging || dragEnd;
  }
};

constexpr uint32_t HOLD_MS = 700;         // hold starts (menu on release)
constexpr uint32_t RESET_HOLD_MS = 3000;     // press the screen in this long to reset the game
constexpr uint32_t BTN_HOLD_SHOW_MS = 900;   // reset countdown appears after this long

namespace input {
void begin();
// Polls all sources; call every loop iteration.
InputEvents poll();
// When enabled, a single click is delayed until the double-click window passes.
void setDoubleClickEnabled(bool on);
void setReversed(bool on);
// True while a finger is on the screen or the button is held.
bool active();
}  // namespace input
