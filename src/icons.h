// Small vector icons drawn with LovyanGFX primitives so they scale and take theme colours.
// Each icon is designed on a 16x16 grid and drawn centred at (cx, cy).
#pragma once
#include "board.h"

enum Icon : uint8_t {
  IC_NONE,
  // Damage and counters
  IC_COMMANDER, IC_POISON, IC_ENERGY, IC_EXPERIENCE, IC_RADIATION, IC_SPEED, IC_TICKETS,
  IC_LIFE, IC_SKULL,
  // Roles and designations
  IC_MONARCH, IC_INITIATIVE, IC_ARCHENEMY, IC_EMPEROR, IC_SCHEMES,
  // Game-wide
  IC_TURN, IC_SUN, IC_MOON, IC_STORM, IC_D20, IC_D6, IC_COIN, IC_FLAG,
  // Menu
  IC_PLAY, IC_REFRESH, IC_UNDO, IC_CARDS, IC_PLAYERS, IC_PARTNERS, IC_THEME, IC_EYE,
  IC_CMD_LIFE, IC_DIAL, IC_LEDS, IC_BACK, IC_MENU,
  // Attacks
  IC_SWORD, IC_LINK, IC_SWATCH, IC_PENCIL,
  // Other card games
  IC_STAR, IC_LORE, IC_PRIZE, IC_TROPHY,
  IC_COUNT
};

// `bg` is used for cut-outs (eyes, sword, holes); pass the colour underneath the icon,
// or the sprite's transparency key so the cut-outs show through.
void drawIcon(lgfx::LovyanGFX &g, Icon ic, int cx, int cy, int size, uint32_t fg, uint32_t bg);
