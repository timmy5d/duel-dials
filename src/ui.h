// Screens, rendering, backlight and LED ring.
#pragma once
#include "board.h"
#include "input.h"

namespace ui {
void begin(LGFX &tft);
void handle(const InputEvents &ev, uint32_t now);
// Renders a new frame if anything changed or a timed element expired.
void update(uint32_t now);
// Debug helper: the frame buffer of the last rendered frame.
LGFX_Sprite &frame();
// Debug helper: draw every icon in a grid (any input returns to the app).
void showGallery();
// Companion app hooks (the web server runs on the main loop, so these are safe to call).
void externalChange();  // the phone changed the game or settings: redraw, LEDs, dial direction
void startGame();       // the phone set up a new game: logo, then the first-player spinner
void whoGoesFirst();
}  // namespace ui
