#include "ui.h"

#include <Adafruit_NeoPixel.h>
#include <math.h>
#include <vector>

#include "game.h"
#include "icons.h"
#include "logo.h"
#include "net.h"
#include "web.h"

namespace {

// ---------------------------------------------------------------- theme
struct Theme {
  uint32_t bg, surface, surface2, text, muted, accent, good, bad, gold;
};

const Theme DARK = {0x000000, 0x1C1C21, 0x2E2E36, 0xFFFFFF, 0x8C8C96,
                    0x4DA3FF, 0x3DDC84, 0xFF5A5F, 0xF5C542};
const Theme LIGHT = {0xF3EFE6, 0xFFFFFF, 0xDCD5C8, 0x18181B, 0x6B6B70,
                     0x1F6FD1, 0x1E9E57, 0xD93A3F, 0xB07D0A};

// The 20 player colours on offer; each player's pick is a palette index stored in the game.
const uint32_t PALETTE[PALETTE_SIZE] = {
    0xE5484D, 0xB3213A, 0xF76B15, 0xF5A524, 0xEEC62B, 0x94C93D, 0x30A46C, 0x0B7E5A, 0x12A594, 0x22B8CF,
    0x5AB2F7, 0x3E8EF7, 0x1F3F9E, 0x5B5BD6, 0x8E4EC6, 0x6A2D9E, 0xD23FBF, 0xF27BA6, 0x8D5B3A, 0x8C8C96};
const char *const PALETTE_NAMES[PALETTE_SIZE] = {
    "Red",  "Crimson", "Orange", "Amber",  "Yellow", "Lime",   "Green",   "Emerald", "Teal",  "Cyan",
    "Sky",  "Blue",    "Navy",   "Indigo", "Violet", "Purple", "Magenta", "Pink",    "Brown", "Gray"};

uint32_t playerColor(int i) { return PALETTE[game.color[i] % PALETTE_SIZE]; }
const uint32_t TEAM_COLORS[2] = {0xF5C542, 0xB8C2CC};
constexpr uint32_t KEY = 0xFF00FF;  // sprite transparency key

const Theme &T() { return settings.theme ? LIGHT : DARK; }

uint32_t mix(uint32_t a, uint32_t b, float t) {
  auto ch = [&](int s) {
    float x = ((a >> s) & 0xFF) * (1 - t) + ((b >> s) & 0xFF) * t;
    return (uint32_t)(x + 0.5f) << s;
  };
  return ch(16) | ch(8) | ch(0);
}

float luminance(uint32_t c) {
  return (0.299f * ((c >> 16) & 0xFF) + 0.587f * ((c >> 8) & 0xFF) + 0.114f * (c & 0xFF)) / 255.f;
}

uint32_t textOn(uint32_t fill) { return luminance(fill) > 0.62f ? 0x18181B : 0xFFFFFF; }

const Icon COUNTER_ICONS[C_COUNT] = {IC_POISON, IC_ENERGY, IC_EXPERIENCE, IC_RADIATION, IC_SPEED, IC_TICKETS};

uint32_t counterColor(int c) {
  static const uint32_t dark[C_COUNT] = {0x5BD15B, 0xFFC93C, 0xB48CFF, 0xC6E83A, 0xFF8A3D, 0xFF7EB6};
  static const uint32_t light[C_COUNT] = {0x2E9E3E, 0xB07D00, 0x7B4FD0, 0x6F8C00, 0xD9631E, 0xD24D8C};
  return settings.theme ? light[c] : dark[c];
}

// The big number's icon and colour for the current game.
Icon lifeIcon() {
  switch (mtg::systemOf(game.format)) {
    case SYS_YUGIOH: return IC_STAR;
    case SYS_LORCANA: return IC_LORE;
    case SYS_POKEMON: return IC_PRIZE;
    case SYS_RIFTBOUND: return IC_TROPHY;
  }
  return IC_LIFE;
}

uint32_t lifeColor() {
  switch (mtg::systemOf(game.format)) {
    case SYS_YUGIOH: return settings.theme ? 0x8A4FD6 : 0xB07CF0;
    case SYS_LORCANA: return settings.theme ? 0x2F64C8 : 0x5B8DEF;
    case SYS_POKEMON: return settings.theme ? 0xB08A00 : 0xEEC62B;
    case SYS_RIFTBOUND: return settings.theme ? 0x0B7E70 : 0x12A594;
  }
  return T().bad;
}

// Drag-to-attack only makes sense where damage lowers life.
bool attacksAllowed() { return mtg::lifeKind() == LK_LIFE; }

// ---------------------------------------------------------------- state
enum Screen { SCR_TABLE, SCR_PLAYER, SCR_MENU, SCR_POPUP, SCR_EDIT, SCR_ATTACK, SCR_SPIN, SCR_VICTORY, SCR_COLOR, SCR_NAME, SCR_KO, SCR_SPLASH, SCR_DICE, SCR_CONNECT, SCR_PHONE };

LGFX *tft = nullptr;
LGFX_Sprite canvas;
LGFX_Sprite center;
LGFX_Sprite label;
Adafruit_NeoPixel leds(NUM_LEDS, PIN_LEDS, NEO_GRB + NEO_KHZ800);

Screen screen = SCR_TABLE;
int focus = 0;
Screen editFrom = SCR_PLAYER;  // list the big editor returns to
int editRow = 0;               // row being edited in the big editor
int editDelta = 0;
uint32_t editDeltaAt = 0;
Icon popupIcon = IC_NONE;

// Victory celebration
constexpr uint32_t VICTORY_ANIM_MS = 12000;  // animate this long, then hold the final frame
constexpr int CONFETTI = 110;
constexpr uint32_t PLAY_AGAIN_DELAY_MS = 1500;  // ignore presses this early so the moment isn't skipped
constexpr int PLAY_BTN_Y = 190;
struct Confetti {
  float x, y, vx, vy;
  uint32_t color;
  uint8_t w, h;
  float phase;
};
Confetti confetti[CONFETTI];

// Knockout celebrations
constexpr uint32_t KO_MS = 3400;
constexpr int KO_PARTS = 40;
Confetti koParts[KO_PARTS];
int koPlayer = 0, koSource = -1;
OutReason koReason = OUT_NONE;
uint32_t koStart = 0, koLastFrame = 0, koGameId = 0;
uint8_t prevOutMask = 0, koQueue = 0;
Screen koReturn = SCR_TABLE;
uint32_t victoryStart = 0, victoryLastFrame = 0;
bool wasOver = false;

// Colour picker
int colorPlayer = 0;
int colorSel = 0;
Screen colorFrom = SCR_TABLE;
bool holdConsumed = false;  // a hold opened the picker; ignore the rest of that hold

// -/+ step buttons (value editor and attack screen); holding one repeats.
constexpr int STEP_R = 22;
bool stepHolding = false;
int stepDir = 0, stepCount = 0;
uint32_t stepLastAt = 0;

// Damage tiles (player screen)
constexpr int TILES_PER_PAGE = 9, TILE_R = 27;

// Name editor
const char NAME_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_'!.&";
constexpr int NAME_LEN = 8;
char nameBuf[NAME_LEN + 1];
int nameCursor = 0;
int namePlayer = 0;
int nameChain = 0;  // >0 while the new-game setup walks through every player's name
void startSpinner();
Screen nameFrom = SCR_TABLE;

uint32_t seenGameId = 0;
int8_t seenFirst = -1;
uint32_t seenAdoptions = 0;
int16_t lastFocusLife = 0;

uint32_t holdShownMs = 0;  // >0 while a hold overlay is up
bool btnHolding = false;   // the overlay is the press-in reset countdown (else the touch-hold hint)
uint32_t lastHoldEventAt = 0;

// First-player spinner
constexpr uint32_t SPIN_MS = 3600;
constexpr uint32_t SPIN_RESULT_MS = 3000;
uint32_t spinStart = 0, spinDoneAt = 0;
float spinFrom = 0, spinTo = 0;
int spinTarget = 0;
bool spinDone = false;

// Drag-to-attack: drag from one seat to another on the table.
bool dragActive = false;
int dragFrom = -1;
int16_t dragX = 0, dragY = 0;

enum AttackType : uint8_t { ATK_DAMAGE, ATK_COMMANDER, ATK_COMMANDER_B, ATK_POISON };
int attackFrom = 0, attackTo = 0;
uint8_t attackType = ATK_DAMAGE;
int attackAmount = 1;
bool attackLifelink = false;
int listSel = 0;
bool editing = false;
bool dirty = true;
uint32_t timedRedrawAt = 0;

int deltaPlayer = -1;
int deltaValue = 0;
uint32_t deltaAt = 0;
constexpr uint32_t DELTA_SHOW_MS = 2500;

char toastText[24] = "";
uint32_t toastAt = 0;
constexpr uint32_t TOAST_MS = 1300;

char popupTitle[24] = "";
char popupText[24] = "";
uint32_t popupAt = 0;

int confirmRow = -1;
uint32_t confirmAt = 0;

uint32_t lastInputAt = 0;
bool asleep = false;
uint32_t lastLedColor = 0xFFFFFFFF;
uint8_t appliedBrightness = 255;

void requestRedraw() { dirty = true; }

// Duel Dials logo: 2 s at power-on and at the start of every new game (here or on a linked dial).
constexpr uint32_t LOGO_MS = 2000;
uint32_t logoUntil = 0;
uint32_t logoGameId = 0;
bool logoOn(uint32_t now) { return (int32_t)(logoUntil - now) > 0; }
void showLogo() {
  logoUntil = millis() + LOGO_MS;
  if (!logoUntil) logoUntil = 1;
  if (!timedRedrawAt || logoUntil < timedRedrawAt) timedRedrawAt = logoUntil;  // repaint underneath when it ends
  dirty = true;
}
void noteNewGame() {
  if (game.gameId == logoGameId) return;
  logoGameId = game.gameId;
  showLogo();
}
void startSpinner();
uint8_t outMask();
uint32_t koColor();
void checkKnockouts(uint32_t now);
void finishKnockout();
void drawKnockout(uint32_t now);
void outlinedText(const char *s, int x, int y, uint32_t fg, uint32_t outline);
float frand(float lo, float hi);
int stepButtonAt(int x, int y);
void applyStep(int d, uint32_t now);
void editStep(int d, uint32_t now);
void drawStepButtons(int y, uint32_t color);
void drawTiles();
void handleTiles(const InputEvents &ev);
void openNameEditor(int player);
void handleName(const InputEvents &ev);
void drawNameEditor();
void openColorPicker(int player);
bool holdOnName(int x, int y);
void handleColor(const InputEvents &ev);
void drawColorPicker();
void showPlayAgainPrompt();
void playAgain();
void drawVictory(uint32_t now);
void checkVictory(uint32_t now);
void finishEdit(int id);
void openSplash(bool resume);
void handleSplash(const InputEvents &ev, uint32_t now);
void drawSplash(uint32_t now);
void openConnect();
void handleConnect(const InputEvents &ev);
void drawConnect();
void openDice();
void handleDice(const InputEvents &ev, uint32_t now);
void drawDice(uint32_t now);

// Row helpers answer for the list that owns the row, even while the big editor is open.
Screen listScreen() { return screen == SCR_EDIT ? editFrom : screen; }
void redrawAt(uint32_t t) {
  if (!timedRedrawAt || t < timedRedrawAt) timedRedrawAt = t;
}

void toast(const char *s) {
  snprintf(toastText, sizeof(toastText), "%s", s);
  toastAt = millis();
  redrawAt(toastAt + TOAST_MS);
  requestRedraw();
}

void popup(const char *title, const char *text) {
  snprintf(popupTitle, sizeof(popupTitle), "%s", title);
  snprintf(popupText, sizeof(popupText), "%s", text);
  popupAt = millis();
  screen = SCR_POPUP;
  requestRedraw();
}

void setBacklight(uint8_t pct) {
  if (pct == appliedBrightness) return;
  appliedBrightness = pct;
  ledcWrite(0, (uint32_t)pct * 255 / 100);
}

float seatAngle(int i) { return 90.f + i * 360.f / game.numPlayers; }

void noteLifeChange(int player, int delta) {
  uint32_t now = millis();
  if (player != deltaPlayer || now - deltaAt > DELTA_SHOW_MS) deltaValue = 0;
  deltaPlayer = player;
  deltaValue += delta;
  deltaAt = now;
  redrawAt(now + DELTA_SHOW_MS);
}

// ---------------------------------------------------------------- list rows
enum PlayerRow {
  R_LIFE = 1,
  R_COUNTER = 10,  // + Counter
  R_SCHEMES = 30,
  R_MONARCH,
  R_INITIATIVE,
  R_CONCEDE,
  R_GAMEMENU,
  R_COLOR,
  R_NAME,
  R_BACK,
  R_CMD = 100,  // + source * 2 + which
};

enum MenuRow {
  M_RESUME,
  M_NEWGAME,
  M_LINK,
  M_MYSEAT,
  M_FORMAT,
  M_PLAYERS,
  M_STARTLIFE,
  M_PARTNERS,
  M_TURN,
  M_DAYNIGHT,
  M_STORM,
  M_FIRST,
  M_D20,
  M_D6,
  M_COIN,
  M_UNDO,
  M_THEME,
  M_BRIGHT,
  M_FACE,
  M_CMDLIFE,
  M_DIAL,
  M_LEDS,
  M_SLEEP,
  M_COUNT
};

const uint8_t SLEEP_CHOICES[] = {0, 1, 2, 5, 10, 30};

std::vector<int> rows;

void buildRows() {
  rows.clear();
  if (screen == SCR_PLAYER && !mtg::isMagic()) {  // other games: just the score and the player
    rows.push_back(R_LIFE);
    rows.push_back(R_NAME);
    rows.push_back(R_COLOR);
    rows.push_back(R_CONCEDE);
    rows.push_back(R_GAMEMENU);
    rows.push_back(R_BACK);
  } else if (screen == SCR_PLAYER) {
    rows.push_back(R_LIFE);
    for (int j = 0; j < game.numPlayers; j++) {
      if (j == focus) continue;
      rows.push_back(R_CMD + j * 2);
      if (game.partners) rows.push_back(R_CMD + j * 2 + 1);
    }
    for (int c = 0; c < C_COUNT; c++) rows.push_back(R_COUNTER + c);
    if (mtg::isArchenemy(focus)) rows.push_back(R_SCHEMES);
    rows.push_back(R_MONARCH);
    rows.push_back(R_INITIATIVE);
    rows.push_back(R_NAME);
    rows.push_back(R_COLOR);
    rows.push_back(R_CONCEDE);
    rows.push_back(R_GAMEMENU);
    rows.push_back(R_BACK);
  } else if (screen == SCR_MENU) {
    rows.push_back(M_RESUME);
    rows.push_back(M_LINK);  // Connectivity
    for (int m = 0; m < M_COUNT; m++) {
      if (m == M_RESUME || m == M_LINK || m == M_MYSEAT) continue;
      bool magicOnly = m == M_PARTNERS || m == M_DAYNIGHT || m == M_STORM || m == M_CMDLIFE;
      if (magicOnly && !mtg::isMagic()) continue;
      if (m == M_STARTLIFE && mtg::lifeKind(settings.format) == LK_RACE) continue;
      rows.push_back(m);
    }
  }
  if (listSel >= (int)rows.size()) listSel = rows.size() - 1;
  if (listSel < 0) listSel = 0;
}

bool rowEditable(int id) {
  if (listScreen() == SCR_PLAYER) {
    return id == R_LIFE || id >= R_CMD || (id >= R_COUNTER && id < R_COUNTER + C_COUNT) || id == R_SCHEMES;
  }
  switch (id) {
    case M_FORMAT: case M_PLAYERS: case M_STARTLIFE: case M_TURN: case M_STORM:
    case M_BRIGHT: case M_SLEEP:
      return true;
  }
  return false;
}

struct RowText {
  char label[28];
  char value[16];
  uint32_t labelColor;
  uint32_t valueColor;
  Icon icon;
  uint32_t iconColor;
};

RowText rowText(int id) {
  RowText r;
  r.label[0] = r.value[0] = 0;
  r.labelColor = T().text;
  r.valueColor = T().text;
  r.icon = IC_NONE;
  r.iconColor = T().muted;
  const Player &p = game.p[focus];
  if (listScreen() == SCR_PLAYER) {
    if (id == R_LIFE) {
      snprintf(r.label, sizeof(r.label), "%s", mtg::lifeName());
      snprintf(r.value, sizeof(r.value), "%d", p.life);
      r.icon = lifeIcon();
      r.iconColor = lifeColor();
      if (mtg::lifeKind() == LK_LIFE && p.life <= 0) r.valueColor = T().bad;
    } else if (id >= R_CMD) {
      int src = (id - R_CMD) / 2, which = (id - R_CMD) % 2;
      char nm[24];
      mtg::playerName(src, nm, sizeof(nm));
      if (game.partners) snprintf(r.label, sizeof(r.label), "%s %c", nm, which ? 'B' : 'A');
      else snprintf(r.label, sizeof(r.label), "%s", nm);
      r.icon = IC_COMMANDER;
      int v = p.cmd[src][which];
      snprintf(r.value, sizeof(r.value), "%d", v);
      r.iconColor = playerColor(src);
      if (v >= COMMANDER_LETHAL) r.valueColor = T().bad;
    } else if (id >= R_COUNTER && id < R_COUNTER + C_COUNT) {
      int c = id - R_COUNTER;
      snprintf(r.label, sizeof(r.label), "%s", mtg::counterName(c));
      r.icon = COUNTER_ICONS[c];
      r.iconColor = counterColor(c);
      if (c == C_POISON) {
        snprintf(r.value, sizeof(r.value), "%d/%d", p.counters[c], POISON_LETHAL);
        if (p.counters[c] >= POISON_LETHAL) r.valueColor = T().bad;
      } else if (c == C_SPEED) {
        snprintf(r.value, sizeof(r.value), p.counters[c] >= 4 ? "MAX" : "%d", p.counters[c]);
      } else {
        snprintf(r.value, sizeof(r.value), "%d", p.counters[c]);
      }
    } else if (id == R_SCHEMES) {
      snprintf(r.label, sizeof(r.label), "Schemes");
      r.icon = IC_SCHEMES;
      r.iconColor = T().bad;
      snprintf(r.value, sizeof(r.value), "%d", game.schemes);
    } else if (id == R_MONARCH) {
      snprintf(r.label, sizeof(r.label), "Monarch");
      r.icon = IC_MONARCH;
      r.iconColor = T().gold;
      snprintf(r.value, sizeof(r.value), game.monarch == focus ? "Yes" : "-");
      if (game.monarch == focus) r.valueColor = T().gold;
    } else if (id == R_INITIATIVE) {
      snprintf(r.label, sizeof(r.label), "Initiative");
      r.icon = IC_INITIATIVE;
      r.iconColor = T().accent;
      snprintf(r.value, sizeof(r.value), game.initiative == focus ? "Yes" : "-");
      if (game.initiative == focus) r.valueColor = T().gold;
    } else if (id == R_CONCEDE) {
      snprintf(r.label, sizeof(r.label), p.conceded ? "Rejoin game" : "Concede");
      r.labelColor = p.conceded ? T().good : T().bad;
      r.icon = IC_FLAG;
      r.iconColor = r.labelColor;
    } else if (id == R_NAME) {
      snprintf(r.label, sizeof(r.label), "Name");
      if (mtg::hasCustomName(focus)) snprintf(r.value, sizeof(r.value), "%s", game.name[focus]);
      else snprintf(r.value, sizeof(r.value), "-");
      r.icon = IC_PENCIL;
      r.iconColor = playerColor(focus);
    } else if (id == R_COLOR) {
      snprintf(r.label, sizeof(r.label), "Color");
      snprintf(r.value, sizeof(r.value), "%s", PALETTE_NAMES[game.color[focus] % PALETTE_SIZE]);
      r.icon = IC_SWATCH;
      r.iconColor = playerColor(focus);
    } else if (id == R_GAMEMENU) {
      snprintf(r.label, sizeof(r.label), "Game menu");
      r.icon = IC_MENU;
    } else if (id == R_BACK) {
      snprintf(r.label, sizeof(r.label), "Back");
      r.labelColor = T().accent;
      r.icon = IC_BACK;
      r.iconColor = T().accent;
    }
    return r;
  }

  auto onoff = [&](bool v) { snprintf(r.value, sizeof(r.value), v ? "On" : "Off"); };
  switch (id) {
    case M_RESUME: snprintf(r.label, sizeof(r.label), "Resume"); r.labelColor = T().accent; break;
    case M_NEWGAME:
      snprintf(r.label, sizeof(r.label), "New game");
      if (confirmRow == M_NEWGAME) { snprintf(r.value, sizeof(r.value), "Sure?"); r.valueColor = T().bad; }
      break;
    case M_LINK:
      snprintf(r.label, sizeof(r.label), "Connectivity");
      if (!settings.linkRoom) snprintf(r.value, sizeof(r.value), "Off");
      else if (settings.mySeat) snprintf(r.value, sizeof(r.value), "R%d P%d", settings.linkRoom, settings.mySeat);
      else snprintf(r.value, sizeof(r.value), "Room %d", settings.linkRoom);
      break;
    case M_MYSEAT:
      snprintf(r.label, sizeof(r.label), "This dial");
      if (!settings.mySeat) snprintf(r.value, sizeof(r.value), "-");
      else snprintf(r.value, sizeof(r.value), "P%d", settings.mySeat);
      break;
    case M_FORMAT: snprintf(r.label, sizeof(r.label), "Format"); snprintf(r.value, sizeof(r.value), "%s", mtg::formatName(settings.format)); break;
    case M_PLAYERS: snprintf(r.label, sizeof(r.label), "Players"); snprintf(r.value, sizeof(r.value), "%d", settings.numPlayers); break;
    case M_STARTLIFE: snprintf(r.label, sizeof(r.label), "Start life"); snprintf(r.value, sizeof(r.value), "%d", settings.startLife); break;
    case M_PARTNERS: snprintf(r.label, sizeof(r.label), "Partners"); onoff(settings.partners); break;
    case M_TURN: snprintf(r.label, sizeof(r.label), "Turn"); snprintf(r.value, sizeof(r.value), "%d", game.turn); break;
    case M_DAYNIGHT:
      snprintf(r.label, sizeof(r.label), "Day/Night");
      snprintf(r.value, sizeof(r.value), game.dayNight == DN_DAY ? "Day" : game.dayNight == DN_NIGHT ? "Night" : "-");
      break;
    case M_STORM: snprintf(r.label, sizeof(r.label), "Storm count"); snprintf(r.value, sizeof(r.value), "%d", game.storm); break;
    case M_FIRST: snprintf(r.label, sizeof(r.label), "Who goes first"); break;
    case M_D20: snprintf(r.label, sizeof(r.label), "Roll d20"); break;
    case M_D6: snprintf(r.label, sizeof(r.label), "Roll d6"); break;
    case M_COIN: snprintf(r.label, sizeof(r.label), "Flip coin"); break;
    case M_UNDO: snprintf(r.label, sizeof(r.label), "Undo"); snprintf(r.value, sizeof(r.value), "%d", mtg::undoDepth()); break;
    case M_THEME: snprintf(r.label, sizeof(r.label), "Theme"); snprintf(r.value, sizeof(r.value), settings.theme ? "Light" : "Dark"); break;
    case M_BRIGHT: snprintf(r.label, sizeof(r.label), "Brightness"); snprintf(r.value, sizeof(r.value), "%d%%", settings.brightness); break;
    case M_FACE: snprintf(r.label, sizeof(r.label), "Face player"); onoff(settings.facePlayer); break;
    case M_CMDLIFE: snprintf(r.label, sizeof(r.label), "Cmdr hits life"); onoff(settings.cmdHitsLife); break;
    case M_DIAL: snprintf(r.label, sizeof(r.label), "Dial"); snprintf(r.value, sizeof(r.value), settings.reverseDial ? "Reversed" : "Normal"); break;
    case M_LEDS: snprintf(r.label, sizeof(r.label), "LED ring"); onoff(settings.leds); break;
    case M_SLEEP:
      snprintf(r.label, sizeof(r.label), "Screen off");
      if (settings.sleepMinutes) snprintf(r.value, sizeof(r.value), "%dm", settings.sleepMinutes);
      else snprintf(r.value, sizeof(r.value), "Never");
      break;
  }
  static const Icon MENU_ICONS[M_COUNT] = {
      IC_PLAY, IC_REFRESH, IC_LINK, IC_DIAL, IC_CARDS, IC_PLAYERS, IC_LIFE, IC_PARTNERS, IC_TURN,
      IC_SUN, IC_STORM, IC_FLAG, IC_D20, IC_D6, IC_COIN, IC_UNDO,
      IC_THEME, IC_SUN, IC_EYE, IC_CMD_LIFE, IC_DIAL, IC_LEDS, IC_MOON};
  r.icon = MENU_ICONS[id];
  if (id == M_RESUME) r.iconColor = T().accent;
  if (id == M_LINK && net::linked()) r.iconColor = T().accent;
  if (id == M_MYSEAT && settings.mySeat) r.iconColor = playerColor(settings.mySeat - 1);
  if (id == M_DAYNIGHT && game.dayNight == DN_NIGHT) r.icon = IC_MOON;
  if (id == M_DAYNIGHT && game.dayNight) r.iconColor = T().gold;
  if (id == M_THEME) r.iconColor = T().text;
  return r;
}

void adjustRow(int id, int d) {
  if (listScreen() == SCR_PLAYER) {
    if (id == R_LIFE) {
      int before = game.p[focus].life;
      mtg::addLife(focus, d * mtg::lifeStep());
      if (game.p[focus].life != before) noteLifeChange(focus, game.p[focus].life - before);
    } else if (id >= R_CMD) {
      int src = (id - R_CMD) / 2, which = (id - R_CMD) % 2;
      int before = game.p[focus].life;
      mtg::addCommanderDamage(focus, src, which, d);
      if (game.p[focus].life != before) noteLifeChange(focus, game.p[focus].life - before);
    } else if (id >= R_COUNTER && id < R_COUNTER + C_COUNT) {
      mtg::addCounter(focus, id - R_COUNTER, d);
    } else if (id == R_SCHEMES) {
      mtg::editGame();
      game.schemes = constrain((int)game.schemes + d, 0, 999);
    }
    return;
  }
  switch (id) {
    case M_LINK: settings.linkRoom = constrain((int)settings.linkRoom + d, 0, 9); break;
    case M_MYSEAT:
      settings.mySeat = constrain((int)settings.mySeat + d, 0, (int)game.numPlayers);
      if (settings.mySeat) focus = settings.mySeat - 1;
      break;
    case M_FORMAT:
      settings.format = (settings.format + FMT_COUNT + (d > 0 ? 1 : -1)) % FMT_COUNT;
      mtg::applyFormatDefaults();
      break;
    case M_PLAYERS:
      settings.numPlayers = constrain((int)settings.numPlayers + d, mtg::minPlayers(settings.format),
                                      mtg::maxPlayers(settings.format));
      break;
    case M_STARTLIFE:
      settings.startLife = constrain((int)settings.startLife + d * (settings.format == FMT_YUGIOH ? 100 : 1), 1, 32000);
      break;
    case M_TURN: mtg::editGame(); game.turn = constrain((int)game.turn + d, 1, 999); break;
    case M_STORM: mtg::editGame(); game.storm = constrain((int)game.storm + d, 0, 999); break;
    case M_BRIGHT: settings.brightness = constrain((int)settings.brightness + d * 5, 10, 100); break;
    case M_SLEEP: {
      int idx = 0;
      for (int i = 0; i < (int)sizeof(SLEEP_CHOICES); i++)
        if (SLEEP_CHOICES[i] == settings.sleepMinutes) idx = i;
      idx = constrain(idx + (d > 0 ? 1 : -1), 0, (int)sizeof(SLEEP_CHOICES) - 1);
      settings.sleepMinutes = SLEEP_CHOICES[idx];
      break;
    }
  }
  mtg::markSettingsDirty();
}

void openTable() {
  screen = SCR_TABLE;
  editing = false;
  requestRedraw();
}

void openPlayer() {
  screen = SCR_PLAYER;
  listSel = 0;
  editing = false;
  mtg::markUndo();
  buildRows();
  requestRedraw();
}

void openMenu() {
  screen = SCR_MENU;
  listSel = 0;
  editing = false;
  confirmRow = -1;
  buildRows();
  requestRedraw();
}

// Current value of a numeric row; `lethal` gets the knockout/max threshold (0 = none).
int rowValue(int id, int *lethal) {
  *lethal = 0;
  const Player &p = game.p[focus];
  if (listScreen() == SCR_PLAYER) {
    if (id == R_LIFE) {
      if (mtg::lifeKind() == LK_RACE && mtg::team(focus) < 0) *lethal = mtg::lifeGoal();  // progress ring
      return p.life;
    }
    if (id >= R_CMD) {
      *lethal = COMMANDER_LETHAL;
      return p.cmd[(id - R_CMD) / 2][(id - R_CMD) % 2];
    }
    if (id >= R_COUNTER && id < R_COUNTER + C_COUNT) {
      int c = id - R_COUNTER;
      if (c == C_POISON) *lethal = POISON_LETHAL;
      if (c == C_SPEED) *lethal = 4;
      return p.counters[c];
    }
    if (id == R_SCHEMES) return game.schemes;
    return 0;
  }
  if (id == M_TURN) return game.turn;
  if (id == M_STORM) return game.storm;
  return 0;
}

// Rows where going up is bad for the player (shown in red while changing).
bool rowIsDamage(int id) {
  if (listScreen() != SCR_PLAYER) return false;
  return id >= R_CMD || id == R_COUNTER + C_POISON || id == R_COUNTER + C_RADIATION;
}

void openEditor(int id) {
  editFrom = screen;
  editRow = id;
  editDelta = 0;
  screen = SCR_EDIT;
  mtg::markUndo();
  requestRedraw();
}

// A choice row finished editing (dial pressed again): apply settings that are costly to change.
void finishEdit(int id) {
  if (listScreen() == SCR_MENU && id == M_LINK) {
    net::setRoom(settings.linkRoom);
    mtg::markSettingsDirty();
  }
}

void pressRow(int id) {
  if (rowEditable(id)) {
    if (screen == SCR_PLAYER || id == M_TURN || id == M_STORM) {
      openEditor(id);
      return;
    }
    editing = !editing;
    if (editing) mtg::markUndo();
    else finishEdit(id);
    return;
  }
  if (screen == SCR_PLAYER) {
    switch (id) {
      case R_MONARCH:
        mtg::markUndo();
        mtg::editGame();
        game.monarch = game.monarch == focus ? -1 : focus;
        break;
      case R_INITIATIVE:
        mtg::markUndo();
        mtg::editGame();
        game.initiative = game.initiative == focus ? -1 : focus;
        break;
      case R_CONCEDE:
        mtg::markUndo();
        mtg::editGame();
        game.p[focus].conceded = !game.p[focus].conceded;
        break;
      case R_GAMEMENU: openMenu(); break;
      case R_COLOR: openColorPicker(focus); break;
      case R_NAME: openNameEditor(focus); break;
      case R_BACK: openTable(); break;
    }
    return;
  }
  uint32_t r = esp_random();
  char buf[24];
  switch (id) {
    case M_RESUME: openTable(); break;
    case M_LINK: openConnect(); break;
    case M_NEWGAME:
      if (confirmRow == M_NEWGAME && millis() - confirmAt < 3000) {
        mtg::newGame();
        focus = 0;
        confirmRow = -1;
        startSpinner();
      } else {
        confirmRow = M_NEWGAME;
        confirmAt = millis();
        redrawAt(confirmAt + 3000);
      }
      break;
    case M_PARTNERS:
      settings.partners = !settings.partners;
      mtg::editGame();
      game.partners = settings.partners;
      mtg::markSettingsDirty();
      break;
    case M_DAYNIGHT:
      mtg::markUndo();
      mtg::editGame();
      game.dayNight = (game.dayNight + 1) % 3;
      break;
    case M_FIRST: startSpinner(); break;
    case M_D20: popupIcon = IC_D20; snprintf(buf, sizeof(buf), "%u", (unsigned)(r % 20 + 1)); popup("d20", buf); break;
    case M_D6: popupIcon = IC_D6; snprintf(buf, sizeof(buf), "%u", (unsigned)(r % 6 + 1)); popup("d6", buf); break;
    case M_COIN: popupIcon = IC_COIN; popup("Coin", (r & 1) ? "Heads" : "Tails"); break;
    case M_UNDO:
      if (mtg::undo()) toast("Undone");
      else toast("Nothing to undo");
      break;
    case M_THEME: settings.theme = !settings.theme; lastLedColor = 0xFFFFFFFF; mtg::markSettingsDirty(); break;
    case M_FACE: settings.facePlayer = !settings.facePlayer; mtg::markSettingsDirty(); break;
    case M_CMDLIFE: settings.cmdHitsLife = !settings.cmdHitsLife; mtg::markSettingsDirty(); break;
    case M_DIAL:
      settings.reverseDial = !settings.reverseDial;
      input::setReversed(settings.reverseDial);
      mtg::markSettingsDirty();
      break;
    case M_LEDS: settings.leds = !settings.leds; lastLedColor = 0xFFFFFFFF; mtg::markSettingsDirty(); break;
  }
}

// Forward declarations (drag-to-attack helpers live near the renderer).
void drawDragLine();
void startSpinner();
void drawSpin(uint32_t now);
void drawHoldOverlay();
void drawTable(uint32_t now, bool ringOnly);
void drawStringFit(LGFX_Sprite &s, const char *str, int x, int y, int maxW, const lgfx::IFont *big, const lgfx::IFont *small);
int hitSeat(int x, int y, float *radius);
int dragTarget(int x, int y);
void openAttack(int from, int to);
void applyAttack();
int attackChipAt(int x, int y);
void tapAttackChip(int chip);
void drawAttack();

// ---------------------------------------------------------------- drawing
void drawStringFit(LGFX_Sprite &s, const char *str, int x, int y, int maxW, const lgfx::IFont *big,
                   const lgfx::IFont *small) {
  s.setFont(big);
  if (s.textWidth(str) > maxW) s.setFont(small);
  s.drawString(str, x, y);
}

struct IconItem {
  Icon icon;
  uint32_t color;
  int value;  // < 0 = icon only
};

// Draws icon+number pairs centred on (cx, y).
void drawIconRow(LGFX_Sprite &s, const IconItem *items, int n, int cx, int y, int iconSize,
                 const lgfx::IFont *font, uint32_t textColor) {
  if (!n) return;
  s.setFont(font);
  char num[8];
  int widths[8], total = 0;
  for (int i = 0; i < n; i++) {
    widths[i] = iconSize;
    if (items[i].value >= 0) {
      snprintf(num, sizeof(num), "%d", items[i].value);
      widths[i] += 3 + s.textWidth(num);
    }
    total += widths[i] + (i ? 10 : 0);
  }
  int x = cx - total / 2;
  s.setTextDatum(textdatum_t::middle_left);
  s.setTextColor(textColor);
  for (int i = 0; i < n; i++) {
    drawIcon(s, items[i].icon, x + iconSize / 2, y, iconSize, items[i].color, KEY);
    if (items[i].value >= 0) {
      snprintf(num, sizeof(num), "%d", items[i].value);
      s.drawString(num, x + iconSize + 3, y + 1);
    }
    x += widths[i] + 10;
  }
  s.setTextDatum(textdatum_t::middle_center);
}

// Up to three of the player's non-zero damage types and counters.
int statusItems(int i, IconItem *out) {
  const Player &p = game.p[i];
  int n = 0;
  int best = 0, src = -1;
  for (int j = 0; j < game.numPlayers; j++)
    for (int k = 0; k < 2; k++)
      if (p.cmd[j][k] > best) best = p.cmd[j][k], src = j;
  if (p.counters[C_POISON]) out[n++] = {IC_POISON, counterColor(C_POISON), p.counters[C_POISON]};
  if (best) out[n++] = {IC_COMMANDER, playerColor(src), best};
  for (int c = 1; c < C_COUNT && n < 3; c++)
    if (p.counters[c]) out[n++] = {COUNTER_ICONS[c], counterColor(c), p.counters[c]};
  return n;
}

void drawBadge(float angDeg, float r, Icon icon, uint32_t fill) {
  float a = angDeg * DEG_TO_RAD;
  int x = 120 + cosf(a) * r, y = 120 + sinf(a) * r;
  canvas.fillCircle(x, y, 11, T().bg);
  canvas.fillCircle(x, y, 10, fill);
  drawIcon(canvas, icon, x, y, 14, textOn(fill), fill);
}

void drawTable(uint32_t now, bool ringOnly) {
  const Theme &th = T();
  canvas.fillScreen(th.bg);
  int n = game.numPlayers;
  float seg = 360.f / n;
  float gap = n > 1 ? 2.5f : 0.f;
  char buf[32];

  for (int i = 0; i < n; i++) {
    float mid = seatAngle(i);
    float a0 = mid - seg / 2 + gap, a1 = mid + seg / 2 - gap;
    bool out = mtg::isOut(i);
    bool f = i == focus;
    bool hasDial = net::seatHasDial(i);
    uint32_t c = playerColor(i);
    if (out) c = mix(th.muted, th.bg, 0.35f);
    uint32_t fill = f ? c : mix(c, th.bg, settings.theme ? 0.55f : 0.5f);
    if (n == 1) canvas.fillCircle(120, 120, 119, fill), canvas.fillCircle(120, 120, f ? 84 : 90, th.bg);
    else canvas.fillArc(120, 120, f ? 84 : 90, 119, a0, a1, fill);
    if (mtg::team(i) >= 0) canvas.fillArc(120, 120, 79, 82, a0, a1, TEAM_COLORS[mtg::team(i)]);

    // Rotated life label facing the player in that seat (with a dial icon if a dial sits there).
    label.fillScreen(KEY);
    if (hasDial) drawIcon(label, IC_DIAL, 9, 14, 12, textOn(fill), KEY);
    label.setTextDatum(textdatum_t::middle_center);
    label.setTextColor(textOn(fill));
    if (out) {
      drawIcon(label, IC_SKULL, hasDial ? 38 : 32, 14, 22, textOn(fill), KEY);
    } else {
      snprintf(buf, sizeof(buf), "%d", game.p[i].life);
      drawStringFit(label, buf, hasDial ? 38 : 32, 14, hasDial ? 46 : 60,
                    n >= 5 && !f ? &fonts::DejaVu18 : &fonts::DejaVu24, &fonts::DejaVu18);
    }
    float rr = f ? 102 : 104;
    float a = mid * DEG_TO_RAD;
    label.pushRotateZoom(&canvas, 120 + cosf(a) * rr, 120 + sinf(a) * rr, mid - 90, 1, 1, KEY);

    float bo = n > 1 ? min(seg / 2 - 8, 16.f) : 16.f;
    if (mtg::isArchenemy(i)) drawBadge(mid, 84, IC_ARCHENEMY, th.bad);
    if (mtg::isEmperor(i)) drawBadge(mid, 84, IC_EMPEROR, TEAM_COLORS[mtg::team(i)]);
    if (game.monarch == i) drawBadge(mid - bo, 84, IC_MONARCH, th.gold);
    if (game.initiative == i) drawBadge(mid + bo, 84, IC_INITIATIVE, th.accent);
  }

  if (ringOnly) return;

  // Centre readout for the focused player.
  center.fillScreen(KEY);
  center.setTextDatum(textdatum_t::middle_center);
  const int cx = center.width() / 2;
  // Day/Night: a big faint sun or moon behind the life total.
  if (game.dayNight == DN_DAY) {
    drawIcon(center, IC_SUN, cx, cx, 152, mix(th.gold, th.bg, 0.8f), KEY);
  } else if (game.dayNight == DN_NIGHT) {
    drawIcon(center, IC_MOON, cx, cx, 146, mix(settings.theme ? 0x3B4A6B : 0xA9B8D9, th.bg, 0.8f), KEY);
  }
  char result[24];
  bool over = mtg::gameResult(result, sizeof(result));
  if (over) {
    center.setTextColor(th.gold);
    drawStringFit(center, result, cx, 26, 120, &fonts::DejaVu18, &fonts::DejaVu12);
  } else {
    mtg::playerName(focus, buf, sizeof(buf));
    center.setTextColor(mtg::isOut(focus) ? th.muted : playerColor(focus));
    drawStringFit(center, buf, cx, 26, 110, &fonts::DejaVu18, &fonts::DejaVu12);
  }

  snprintf(buf, sizeof(buf), "%d", game.p[focus].life);
  center.setTextColor(mtg::isOut(focus) ? th.muted : th.text);
  drawStringFit(center, buf, cx, 76, 140, &fonts::DejaVu72, &fonts::DejaVu56);

  center.setFont(&fonts::DejaVu24);
  bool deltaOn = deltaPlayer == focus && deltaValue != 0 && now - deltaAt < DELTA_SHOW_MS;
  bool toastOn = toastText[0] && now - toastAt < TOAST_MS;
  if (toastOn) {
    center.setTextColor(th.accent);
    drawStringFit(center, toastText, cx, 120, 120, &fonts::DejaVu18, &fonts::DejaVu12);
  } else if (deltaOn) {
    snprintf(buf, sizeof(buf), "%+d", deltaValue);
    center.setTextColor(deltaValue > 0 ? th.good : th.bad);
    center.drawString(buf, cx, 120);
  } else {
    OutReason why = mtg::outReason(focus);
    if (why != OUT_NONE) {
      const char *w = why == OUT_POISON ? "Poisoned" : why == OUT_COMMANDER ? "Cmdr damage"
                      : why == OUT_CONCEDE ? "Conceded" : "Out";
      Icon wi = why == OUT_POISON ? IC_POISON : why == OUT_COMMANDER ? IC_COMMANDER
                : why == OUT_CONCEDE ? IC_FLAG : IC_SKULL;
      center.setFont(&fonts::DejaVu18);
      int tw = center.textWidth(w) + 20;
      drawIcon(center, wi, cx - tw / 2 + 8, 120, 16, th.bad, KEY);
      center.setTextColor(th.bad);
      center.setTextDatum(textdatum_t::middle_left);
      center.drawString(w, cx - tw / 2 + 20, 121);
      center.setTextDatum(textdatum_t::middle_center);
    } else if (!mtg::isMagic()) {
      // What the number means: LP, lore toward 20, prizes left, points toward 8 (or the team's 11).
      char g[28];
      int tm = mtg::team(focus);
      if (tm >= 0) snprintf(g, sizeof(g), "Team %c %d/%d", 'A' + tm, mtg::teamScore(tm), mtg::lifeGoal());
      else if (mtg::lifeKind() == LK_RACE) snprintf(g, sizeof(g), "of %d %s", mtg::lifeGoal(), mtg::lifeName());
      else if (mtg::lifeKind() == LK_PRIZES) snprintf(g, sizeof(g), "prizes left");
      else snprintf(g, sizeof(g), "%s", mtg::lifeName());
      for (char *c = g; *c; c++)
        if (tm < 0 && mtg::lifeKind() == LK_RACE && c > g + 3) *c = tolower(*c);
      center.setFont(&fonts::DejaVu18);
      int tw = center.textWidth(g) + 20;
      drawIcon(center, lifeIcon(), cx - tw / 2 + 8, 120, 16, lifeColor(), KEY);
      center.setTextColor(th.muted);
      center.setTextDatum(textdatum_t::middle_left);
      center.drawString(g, cx - tw / 2 + 20, 121);
      center.setTextDatum(textdatum_t::middle_center);
    } else {
      IconItem items[4];
      int ni = statusItems(focus, items);
      drawIconRow(center, items, ni, cx, 120, 16, &fonts::DejaVu18, th.text);
    }
  }

  IconItem info[3];
  int ninfo = 0;
  info[ninfo++] = {IC_TURN, th.muted, game.turn};
  if (game.storm) info[ninfo++] = {IC_STORM, th.accent, game.storm};
  if (net::linked()) info[ninfo++] = {IC_LINK, net::peerCount() ? th.accent : th.muted, net::peerCount() + 1};
  drawIconRow(center, info, ninfo, cx, 143, 11, &fonts::DejaVu12, th.muted);

  float rot = settings.facePlayer && game.numPlayers > 1 ? seatAngle(focus) - 90 : 0;
  center.pushRotateZoom(&canvas, 120, 120, rot, 1, 1, KEY);

  if (dragActive) drawDragLine();
}

void drawList(const char *title, uint32_t titleColor) {
  const Theme &th = T();
  canvas.fillScreen(th.bg);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(titleColor);
  drawStringFit(canvas, title, 120, 22, 120, &fonts::DejaVu18, &fonts::DejaVu12);

  const int rowH = 36, cy = 126;
  int count = rows.size();
  for (int k = -2; k <= 2; k++) {
    int idx = listSel + k;
    if (idx < 0 || idx >= count) continue;
    int y = cy + k * rowH;
    int dy = abs(y - 120) + 14;
    int hw = (int)sqrtf(118.f * 118.f - dy * dy) - 15;
    RowText r = rowText(rows[idx]);
    bool sel = k == 0;
    if (sel) {
      uint32_t pill = editing ? th.accent : th.surface2;
      canvas.fillRoundRect(120 - hw, y - 16, hw * 2, 32, 16, pill);
      if (editing) r.labelColor = r.valueColor = textOn(pill);
    }
    uint32_t lc = sel ? r.labelColor : mix(r.labelColor, th.bg, abs(k) == 1 ? 0.35f : 0.6f);
    uint32_t vc = sel ? r.valueColor : mix(r.valueColor, th.bg, abs(k) == 1 ? 0.35f : 0.6f);
    int lx = 120 - hw + 14;
    int rx = 120 + hw - 14;
    if (r.icon) {
      int isz = sel ? 18 : 14;
      uint32_t ic = sel ? (editing ? textOn(th.accent) : r.iconColor)
                        : mix(r.iconColor, th.bg, abs(k) == 1 ? 0.35f : 0.6f);
      uint32_t under = sel ? (editing ? th.accent : th.surface2) : th.bg;
      drawIcon(canvas, r.icon, lx + isz / 2, y, isz, ic, under);
      lx += isz + 7;
    }
    // Pick the largest font where label and value both fit on this row.
    const lgfx::IFont *fontsBySize[] = {&fonts::DejaVu18, &fonts::DejaVu12, &fonts::DejaVu9};
    bool fits = false;
    for (int fi = sel ? 0 : 1; fi < 3 && !fits; fi++) {
      canvas.setFont(fontsBySize[fi]);
      int need = canvas.textWidth(r.label) + (r.value[0] ? canvas.textWidth(r.value) + 10 : 0);
      fits = need <= rx - lx;
    }
    if (!fits && !sel) r.value[0] = 0;  // edge rows are narrow: drop the value rather than overlap
    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.setTextColor(lc);
    canvas.drawString(r.label, lx, y + 1);
    if (r.value[0]) {
      canvas.setTextDatum(textdatum_t::middle_right);
      canvas.setTextColor(vc);
      canvas.drawString(r.value, rx, y + 1);
    }
  }

  // Scroll position indicator on the right edge.
  if (count > 1) {
    canvas.fillArc(120, 120, 113, 116, -35, 35, th.surface2);
    float pos = -35 + 70.f * listSel / (count - 1);
    canvas.fillArc(120, 120, 112, 117, pos - 4, pos + 4, th.accent);
  }
}

void drawEditor(uint32_t now) {
  const Theme &th = T();
  canvas.fillScreen(th.bg);
  RowText r = rowText(editRow);
  int lethal;
  int v = rowValue(editRow, &lethal);
  bool goal = listScreen() == SCR_PLAYER && editRow == R_LIFE && lethal && v >= lethal;
  bool dead = lethal && v >= lethal && editRow != R_COUNTER + C_SPEED && !goal;
  uint32_t ic = dead ? th.bad : goal ? th.gold : r.iconColor;

  // The damage type's symbol, large and faint, behind the number.
  drawIcon(canvas, r.icon, 120, 124, 178, mix(ic, th.bg, settings.theme ? 0.8f : 0.76f), th.bg);

  // Progress toward lethal (or the maximum) around the rim.
  if (lethal) {
    canvas.fillArc(120, 120, 111, 117, 0, 360, th.surface2);
    float frac = min(1.f, (float)v / lethal);
    if (frac > 0) canvas.fillArc(120, 120, 110, 118, -90, -90 + 360 * frac, ic);
  }

  canvas.setTextDatum(textdatum_t::middle_center);
  char title[32];
  if (listScreen() == SCR_PLAYER && editRow >= R_CMD) {
    int src = (editRow - R_CMD) / 2;
    char nm[12];
    mtg::shortName(src, nm, sizeof(nm));
    if (game.partners) snprintf(title, sizeof(title), "Cmdr %s %c", nm, (editRow - R_CMD) % 2 ? 'B' : 'A');
    else snprintf(title, sizeof(title), "Cmdr %s", nm);
  } else {
    snprintf(title, sizeof(title), "%s", r.label);
  }
  canvas.setTextColor(ic);
  drawStringFit(canvas, title, 120, 40, 150, &fonts::DejaVu18, &fonts::DejaVu12);

  char num[12];
  snprintf(num, sizeof(num), "%d", v);
  canvas.setTextColor(dead ? th.bad : th.text);
  drawStringFit(canvas, num, 120, 118, 120, &fonts::DejaVu72, &fonts::DejaVu56);
  drawStepButtons(118, ic);

  bool deltaOn = editDelta && now - editDeltaAt < DELTA_SHOW_MS;
  if (deltaOn) {
    snprintf(num, sizeof(num), "%+d", editDelta);
    bool bad = rowIsDamage(editRow) ? editDelta > 0 : editDelta < 0;
    canvas.setTextColor(bad ? th.bad : th.good);
    canvas.setFont(&fonts::DejaVu24);
    canvas.drawString(num, 120, 170);
  } else if (dead || goal) {
    canvas.setTextColor(goal ? th.gold : th.bad);
    canvas.setFont(&fonts::DejaVu18);
    canvas.drawString(goal ? "Winner!" : "Lethal", 120, 170);
  } else if (lethal) {
    snprintf(num, sizeof(num), editRow == R_COUNTER + C_SPEED ? "max %d" : "of %d", lethal);
    canvas.setTextColor(th.muted);
    canvas.setFont(&fonts::DejaVu18);
    canvas.drawString(num, 120, 170);
  }
  if (listScreen() == SCR_PLAYER) {
    char who[24];
    mtg::playerName(focus, who, sizeof(who));
    canvas.setTextColor(playerColor(focus));
    canvas.setFont(&fonts::DejaVu12);
    canvas.drawString(who, 120, 190);
  }
  canvas.fillRoundRect(80, 200, 80, 26, 13, th.accent);
  canvas.setTextColor(textOn(th.accent));
  canvas.setFont(&fonts::DejaVu12);
  canvas.drawString("Done", 120, 214);
}

void drawPopup() {
  const Theme &th = T();
  canvas.fillScreen(th.bg);
  canvas.fillCircle(120, 120, 96, th.surface);
  if (popupIcon) drawIcon(canvas, popupIcon, 120, 48, 26, th.accent, th.surface);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(th.muted);
  drawStringFit(canvas, popupTitle, 120, 80, 150, &fonts::DejaVu18, &fonts::DejaVu12);
  canvas.setTextColor(th.text);
  drawStringFit(canvas, popupText, 120, 124, 170, &fonts::DejaVu56, &fonts::DejaVu24);
  canvas.setTextColor(th.muted);
  canvas.setFont(&fonts::DejaVu9);
  canvas.drawString("press or tap", 120, 178);
}

// ---------------------------------------------------------------- drag to attack
void seatPoint(int i, float r, int *x, int *y) {
  float a = seatAngle(i) * DEG_TO_RAD;
  *x = 120 + cosf(a) * r;
  *y = 120 + sinf(a) * r;
}

// Seat under a drag release point, or -1 when it's not a valid target.
int dragTarget(int x, int y) {
  float r;
  int to = hitSeat(x, y, &r);
  if (r < 40 || to == dragFrom) return -1;
  return to;
}

void drawDragLine() {
  const Theme &th = T();
  uint32_t col = playerColor(dragFrom);
  int to = dragTarget(dragX, dragY);
  int x0, y0;
  seatPoint(dragFrom, 100, &x0, &y0);
  int x1 = dragX, y1 = dragY;
  if (to >= 0) {
    // Outline the seat being targeted and snap the arrow to it.
    float seg = 360.f / game.numPlayers, mid = seatAngle(to);
    canvas.fillArc(120, 120, 74, 80, mid - seg / 2 + 3, mid + seg / 2 - 3, th.text);
    seatPoint(to, 86, &x1, &y1);
  }
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  canvas.fillCircle(x0, y0, 8, th.bg);
  canvas.fillCircle(x0, y0, 6, col);
  if (len < 12) return;
  float ux = dx / len, uy = dy / len;
  int bx = x1 - ux * 14, by = y1 - uy * 14;  // base of the arrow head
  canvas.drawWideLine(x0, y0, bx, by, 5.5f, th.bg);
  canvas.drawWideLine(x0, y0, bx, by, 3.5f, col);
  float nx = -uy, ny = ux;
  canvas.fillTriangle(x1 + ux * 3, y1 + uy * 3, bx + nx * 12, by + ny * 12, bx - nx * 12, by - ny * 12, th.bg);
  canvas.fillTriangle(x1, y1, bx + nx * 9, by + ny * 9, bx - nx * 9, by - ny * 9, col);
}

// Chips along the bottom of the attack screen: damage types, then lifelink.
int attackChips(uint8_t *types) {
  int n = 0;
  types[n++] = ATK_DAMAGE;
  if (!mtg::isMagic()) return n;
  types[n++] = ATK_COMMANDER;
  if (game.partners) types[n++] = ATK_COMMANDER_B;
  types[n++] = ATK_POISON;
  types[n++] = 0xFF;  // lifelink toggle
  return n;
}

const int CHIP_Y = 192, CHIP_STEP = 38, CHIP_R = 16;

int chipX(int i, int n) { return 120 - (n - 1) * CHIP_STEP / 2 + i * CHIP_STEP; }

int attackChipAt(int x, int y) {
  uint8_t types[6];
  int n = attackChips(types);
  if (abs(y - CHIP_Y) > CHIP_R + 6) return -1;
  for (int i = 0; i < n; i++)
    if (abs(x - chipX(i, n)) <= CHIP_STEP / 2) return i;
  return -1;
}

void tapAttackChip(int chip) {
  uint8_t types[6];
  attackChips(types);
  if (types[chip] == 0xFF) attackLifelink = !attackLifelink;
  else attackType = types[chip];
}

Icon attackIcon(uint8_t t) {
  return t == ATK_POISON ? IC_POISON : (t == ATK_COMMANDER || t == ATK_COMMANDER_B) ? IC_COMMANDER : IC_SWORD;
}

uint32_t attackColor(uint8_t t) {
  if (t == ATK_POISON) return counterColor(C_POISON);
  if (t == ATK_DAMAGE) return T().bad;
  return playerColor(attackFrom);
}

void openAttack(int from, int to) {
  attackFrom = from;
  attackTo = to;
  attackAmount = game.format == FMT_YUGIOH ? 1000 : 1;
  attackLifelink = false;
  if (!mtg::isMagic()) attackType = ATK_DAMAGE;  // lifelink is per creature, so it never carries over
  if (attackType == ATK_COMMANDER_B && !game.partners) attackType = ATK_COMMANDER;
  screen = SCR_ATTACK;
  requestRedraw();
}

void applyAttack() {
  int n = attackAmount;
  if (n > 0) {
    mtg::markUndo();  // the whole attack is one undo step
    int before = game.p[attackTo].life;
    switch (attackType) {
      case ATK_DAMAGE: mtg::addLife(attackTo, -n); break;
      case ATK_COMMANDER: mtg::addCommanderDamage(attackTo, attackFrom, 0, n); break;
      case ATK_COMMANDER_B: mtg::addCommanderDamage(attackTo, attackFrom, 1, n); break;
      case ATK_POISON: mtg::addCounter(attackTo, C_POISON, n); break;
    }
    if (attackLifelink) mtg::addLife(attackFrom, n);
    focus = attackTo;
    if (game.p[attackTo].life != before) noteLifeChange(attackTo, game.p[attackTo].life - before);
    else toast(attackType == ATK_POISON ? "Poisoned" : "Hit");
  }
  openTable();
}

void drawAttack() {
  const Theme &th = T();
  canvas.fillScreen(th.bg);
  uint32_t tc = attackColor(attackType);
  drawIcon(canvas, attackIcon(attackType), 120, 112, 150, mix(tc, th.bg, settings.theme ? 0.82f : 0.78f), th.bg);

  // "P1 -> P3" header in each player's colour.
  char a[12], b[12];
  mtg::shortName(attackFrom, a, sizeof(a));
  mtg::shortName(attackTo, b, sizeof(b));
  canvas.setFont(&fonts::DejaVu24);
  if (canvas.textWidth(a) + canvas.textWidth(b) + 30 > 150) canvas.setFont(&fonts::DejaVu18);
  if (canvas.textWidth(a) + canvas.textWidth(b) + 30 > 150) canvas.setFont(&fonts::DejaVu12);
  int wa = canvas.textWidth(a), wb = canvas.textWidth(b), gap = 30;
  int x = 120 - (wa + gap + wb) / 2;
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.setTextColor(playerColor(attackFrom));
  canvas.drawString(a, x, 34);
  int ax = x + wa + 6;
  canvas.fillRect(ax, 32, 12, 4, th.text);
  canvas.fillTriangle(ax + 12, 26, ax + 12, 42, ax + 19, 34, th.text);
  canvas.setTextColor(playerColor(attackTo));
  canvas.drawString(b, x + wa + gap, 34);

  // Amount
  char num[12];
  snprintf(num, sizeof(num), "%d", attackAmount);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(th.text);
  drawStringFit(canvas, num, 120, 100, 120, &fonts::DejaVu72, &fonts::DejaVu40);
  drawStepButtons(100, tc);

  // What will happen
  const Player &t = game.p[attackTo];
  char line1[40], line2[40] = "";
  int n = attackAmount;
  switch (attackType) {
    case ATK_DAMAGE:
      snprintf(line1, sizeof(line1), "%s %d > %d", mtg::lifeName(), t.life,
               game.format == FMT_YUGIOH ? max(0, t.life - n) : t.life - n);
      break;
    case ATK_COMMANDER:
    case ATK_COMMANDER_B: {
      int w = attackType == ATK_COMMANDER_B;
      int c = t.cmd[attackFrom][w];
      snprintf(line1, sizeof(line1), "Cmdr%s %d > %d", game.partners ? (w ? " B" : " A") : "", c, c + n);
      if (settings.cmdHitsLife) snprintf(line2, sizeof(line2), "Life %d > %d", t.life, t.life - n);
      break;
    }
    case ATK_POISON:
      snprintf(line1, sizeof(line1), "Poison %d > %d", t.counters[C_POISON], t.counters[C_POISON] + n);
      break;
  }
  if (attackLifelink) {
    int l = strlen(line2);
    char nm[12];
    mtg::shortName(attackFrom, nm, sizeof(nm));
    snprintf(line2 + l, sizeof(line2) - l, "%s%s +%d", l ? "   " : "", nm, n);
  }
  canvas.setFont(&fonts::DejaVu18);
  canvas.setTextColor(tc);
  canvas.drawString(line1, 120, 144);
  canvas.setFont(&fonts::DejaVu12);
  canvas.setTextColor(th.muted);
  if (line2[0]) canvas.drawString(line2, 120, 163);

  // Type chips + lifelink
  uint8_t types[6];
  int nc = attackChips(types);
  for (int i = 0; i < nc; i++) {
    int cx = chipX(i, nc);
    bool lifelink = types[i] == 0xFF;
    bool on = lifelink ? attackLifelink : types[i] == attackType;
    uint32_t col = lifelink ? th.good : attackColor(types[i]);
    uint32_t fill = on ? col : th.surface2;
    canvas.fillCircle(cx, CHIP_Y, CHIP_R, fill);
    uint32_t fg = on ? textOn(fill) : col;
    drawIcon(canvas, lifelink ? IC_LIFE : attackIcon(types[i]), cx, CHIP_Y, 18, fg, fill);
    if (types[i] == ATK_COMMANDER_B || (types[i] == ATK_COMMANDER && game.partners)) {
      canvas.setFont(&fonts::DejaVu9);
      canvas.setTextColor(fg);
      canvas.drawString(types[i] == ATK_COMMANDER_B ? "B" : "A", cx + 10, CHIP_Y + 10);
    }
  }
  canvas.setFont(&fonts::DejaVu9);
  canvas.setTextColor(th.muted);
  canvas.drawString(attackLifelink ? "lifelink on" : "press to apply", 120, 222);
}

// ---------------------------------------------------------------- spinner + hold-to-reset
int seatAtAngle(float a) {
  float rel = fmodf(a - 90.f + 3600.f, 360.f);
  return ((int)floorf(rel / (360.f / game.numPlayers) + 0.5f)) % game.numPlayers;
}

// Animate the spinner landing on `target` (also used when another dial started the game).
void spinLandOn(int target) {
  if (game.format == FMT_ARCHENEMY) {  // the Archenemy always takes the first turn
    focus = 0;
    openTable();
    toast("Archenemy first");
    return;
  }
  if (target < 0 || target >= game.numPlayers) {
    openTable();
    return;
  }
  spinTarget = target;
  float seg = 360.f / game.numPlayers;
  float jitter = ((esp_random() % 1000) / 1000.f - 0.5f) * seg * 0.6f;  // land off-centre
  spinFrom = seatAngle(focus);
  float delta = fmodf(seatAngle(spinTarget) + jitter - spinFrom + 720.f, 360.f);
  spinTo = spinFrom + 360.f * (3 + esp_random() % 2) + delta;
  noteNewGame();
  spinStart = logoOn(millis()) ? logoUntil : millis();  // spin once the logo has gone
  spinDone = false;
  screen = SCR_SPIN;
  requestRedraw();
}

void startSpinner() {
  int alive[MAX_PLAYERS], n = 0;
  for (int i = 0; i < game.numPlayers; i++)
    if (!mtg::isOut(i)) alive[n++] = i;
  int target = game.format == FMT_ARCHENEMY ? 0 : (n ? alive[esp_random() % n] : 0);
  mtg::setFirstPlayer(target);  // shared with linked dials
  seenGameId = game.gameId;
  seenFirst = target;
  spinLandOn(target);
}

// Called every frame: react to games that arrived from other dials.
void syncFromLink(uint32_t now) {
  if (net::takeJoinAdopted()) {
    logoGameId = game.gameId;
    seenGameId = game.gameId;
    seenFirst = game.firstPlayer;
    if (screen == SCR_SPIN || screen == SCR_ATTACK || screen == SCR_EDIT) openTable();
    toast("Joined room");
    char tmp[24];
    wasOver = mtg::gameResult(tmp, sizeof(tmp));  // don't celebrate a game that ended before we joined
    prevOutMask = outMask();
    koGameId = game.gameId;
  }
  if (game.gameId != seenGameId || game.firstPlayer != seenFirst) {
    bool fresh = game.gameId != seenGameId;
    if (fresh) nameChain = 0;  // another dial started a game mid-setup
    seenGameId = game.gameId;
    seenFirst = game.firstPlayer;
    if (game.firstPlayer >= 0) {
      spinLandOn(game.firstPlayer);
    } else if (fresh) {
      focus = settings.mySeat ? settings.mySeat - 1 : 0;
      openTable();
    }
  }
  static uint32_t seenPeers = 0;
  if (net::peerSignature() != seenPeers) {
    seenPeers = net::peerSignature();
    requestRedraw();
  }
  uint32_t a = net::adoptions();
  if (a != seenAdoptions) {
    seenAdoptions = a;
    if (focus >= game.numPlayers) focus = 0;
    if (screen == SCR_ATTACK && (attackFrom >= game.numPlayers || attackTo >= game.numPlayers)) openTable();
    if (game.p[focus].life != lastFocusLife && screen == SCR_TABLE)
      noteLifeChange(focus, game.p[focus].life - lastFocusLife);
    requestRedraw();
  }
  lastFocusLife = game.p[focus].life;
}

float spinAngle(uint32_t now) {
  float t = min(1.f, (now - spinStart) / (float)SPIN_MS);
  float e = 1.f - powf(1.f - t, 3.5f);  // ease out
  return spinFrom + (spinTo - spinFrom) * e;
}

void drawSpin(uint32_t now) {
  const Theme &th = T();
  float a = spinAngle(now);
  focus = spinDone ? spinTarget : seatAtAngle(a);  // ring highlight and LEDs follow the arrow
  drawTable(now, true);
  uint32_t col = playerColor(focus);
  float r = a * DEG_TO_RAD, ux = cosf(r), uy = sinf(r), nx = -uy, ny = ux;

  if (!spinDone) {
    int tx = 120 + ux * 76, ty = 120 + uy * 76;      // tip
    int bx = 120 + ux * 52, by = 120 + uy * 52;      // head base
    int qx = 120 - ux * 26, qy = 120 - uy * 26;      // tail
    canvas.drawWideLine(qx, qy, bx, by, 7.5f, th.bg);
    canvas.drawWideLine(qx, qy, bx, by, 5.5f, col);
    canvas.fillTriangle(tx + ux * 3, ty + uy * 3, bx + nx * 18, by + ny * 18, bx - nx * 18, by - ny * 18, th.bg);
    canvas.fillTriangle(tx, ty, bx + nx * 15, by + ny * 15, bx - nx * 15, by - ny * 15, col);
    canvas.fillCircle(120, 120, 14, th.surface2);
    canvas.fillCircle(120, 120, 8, col);
    return;
  }
  // Result: a disc naming the first player, with a short pointer to their seat.
  int tx = 120 + ux * 80, ty = 120 + uy * 80;
  int bx = 120 + ux * 56, by = 120 + uy * 56;
  canvas.fillTriangle(tx, ty, bx + nx * 14, by + ny * 14, bx - nx * 14, by - ny * 14, col);
  canvas.fillCircle(120, 120, 60, th.surface);
  canvas.drawCircle(120, 120, 60, col);
  drawIcon(canvas, IC_FLAG, 120, 90, 22, col, th.surface);
  char name[24];
  mtg::playerName(focus, name, sizeof(name));
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(col);
  drawStringFit(canvas, name, 120, 120, 104, &fonts::DejaVu18, &fonts::DejaVu12);
  canvas.setTextColor(th.muted);
  canvas.setFont(&fonts::DejaVu12);
  canvas.drawString("goes first", 120, 144);
}

void resetToDefault() {
  focus = 0;
  editing = false;
  dragActive = false;
  openSplash(false);  // pick the next game type
}

void drawHoldOverlay() {
  const Theme &th = T();
  if (!btnHolding) {  // finger resting on the glass: just say what letting go does
    if (screen != SCR_TABLE && screen != SCR_PLAYER) return;
    canvas.fillRoundRect(54, 196, 132, 26, 13, th.surface2);
    drawIcon(canvas, IC_MENU, 72, 209, 12, th.text, th.surface2);
    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.setTextColor(th.text);
    canvas.setFont(&fonts::DejaVu12);
    canvas.drawString("release for menu", 82, 210);
    canvas.setTextDatum(textdatum_t::middle_center);
    return;
  }
  float frac = min(1.f, (holdShownMs - BTN_HOLD_SHOW_MS) / (float)(RESET_HOLD_MS - BTN_HOLD_SHOW_MS));
  canvas.fillArc(120, 120, 111, 119, 0, 360, th.surface2);
  if (frac > 0) canvas.fillArc(120, 120, 110, 119, -90, -90 + 360 * frac, th.bad);
  canvas.fillRoundRect(32, 78, 176, 84, 30, th.surface);
  canvas.drawRoundRect(32, 78, 176, 84, 30, th.bad);
  int secs = (RESET_HOLD_MS - holdShownMs + 999) / 1000;
  char buf[24];
  snprintf(buf, sizeof(buf), "Reset in %d", secs);
  canvas.setFont(&fonts::DejaVu18);
  int w = canvas.textWidth(buf) + 26;
  drawIcon(canvas, IC_REFRESH, 120 - w / 2 + 10, 104, 20, th.bad, th.surface);
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.setTextColor(th.text);
  canvas.drawString(buf, 120 - w / 2 + 26, 105);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setFont(&fonts::DejaVu12);
  canvas.setTextColor(th.text);
  canvas.drawString("Choose a new game", 120, 128);
  canvas.setTextColor(th.muted);
  canvas.setFont(&fonts::DejaVu9);
  canvas.drawString("let go to cancel", 120, 146);
}

// ---------------------------------------------------------------- colour picker
// Where the focused player's name is drawn on the table (the centre readout may be rotated).
bool holdOnName(int x, int y) {
  if (screen == SCR_PLAYER) return y < 44;
  if (screen != SCR_TABLE) return false;
  float rot = settings.facePlayer && game.numPlayers > 1 ? seatAngle(focus) - 90 : 0;
  float r = rot * DEG_TO_RAD;
  float nx = 120 + 52 * sinf(r), ny = 120 - 52 * cosf(r);
  float dx = x - nx, dy = y - ny;
  return dx * dx + dy * dy <= 34 * 34;
}

void openColorPicker(int player) {
  colorPlayer = player;
  colorSel = game.color[player] % PALETTE_SIZE;
  colorFrom = screen == SCR_PLAYER ? SCR_PLAYER : SCR_TABLE;
  editing = false;
  screen = SCR_COLOR;
  requestRedraw();
}

void closeColorPicker(bool apply) {
  if (apply && colorSel != game.color[colorPlayer]) {
    mtg::markUndo();
    mtg::editGame();  // saved, undoable and shared with linked dials
    game.color[colorPlayer] = colorSel;
  }
  screen = colorFrom;
  if (screen == SCR_PLAYER) buildRows();
  requestRedraw();
}

void swatchPos(int k, int *x, int *y) {
  float a = (-90 + k * 360.f / PALETTE_SIZE) * DEG_TO_RAD;
  *x = 120 + cosf(a) * 97;
  *y = 120 + sinf(a) * 97;
}

void handleColor(const InputEvents &ev) {
  if (ev.detents) colorSel = ((colorSel + ev.detents) % PALETTE_SIZE + PALETTE_SIZE) % PALETTE_SIZE;
  if (ev.click) closeColorPicker(true);
  if (ev.longPress) closeColorPicker(false);
  if (ev.tap) {
    float dx = ev.x - 120, dy = ev.y - 120;
    float r = sqrtf(dx * dx + dy * dy);
    if (r < 64 && ev.y < 118) {
      openNameEditor(colorPlayer);  // tap the name to rename
    } else if (r < 64) {
      closeColorPicker(true);
    } else if (r > 78) {
      float a = atan2f(dy, dx) * RAD_TO_DEG + 90;
      colorSel = ((int)floorf(a / (360.f / PALETTE_SIZE) + 0.5f) % PALETTE_SIZE + PALETTE_SIZE) % PALETTE_SIZE;
    }
  }
}

void drawColorPicker() {
  const Theme &th = T();
  canvas.fillScreen(th.bg);
  for (int k = 0; k < PALETTE_SIZE; k++) {
    int x, y;
    swatchPos(k, &x, &y);
    if (k == colorSel) {
      canvas.fillCircle(x, y, 15, th.text);
      canvas.fillCircle(x, y, 13, th.bg);
      canvas.fillCircle(x, y, 11, PALETTE[k]);
    } else {
      canvas.fillCircle(x, y, 10, PALETTE[k]);
    }
    // A dot marks colours other players already use.
    for (int j = 0; j < game.numPlayers; j++)
      if (j != colorPlayer && game.color[j] % PALETTE_SIZE == k) canvas.fillCircle(x, y, 3, textOn(PALETTE[k]));
  }
  uint32_t c = PALETTE[colorSel];
  canvas.fillCircle(120, 120, 62, c);
  uint32_t fg = textOn(c);
  char name[24];
  mtg::playerName(colorPlayer, name, sizeof(name));
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(fg);
  canvas.setFont(&fonts::DejaVu18);
  if (canvas.textWidth(name) > 86) canvas.setFont(&fonts::DejaVu12);
  int nw = canvas.textWidth(name) + 16;
  canvas.drawString(name, 120 - 8, 100);
  drawIcon(canvas, IC_PENCIL, 120 + nw / 2 - 4, 100, 12, fg, c);  // tap the name to rename
  canvas.drawFastHLine(120 - nw / 2, 112, nw, mix(fg, c, 0.5f));
  canvas.setFont(&fonts::DejaVu24);
  canvas.drawString(PALETTE_NAMES[colorSel], 120, 134);
  canvas.setFont(&fonts::DejaVu9);
  canvas.setTextColor(mix(fg, c, 0.35f));
  canvas.drawString("press to choose", 120, 158);
  canvas.drawString("tap name to rename", 120, 170);
}

// ---------------------------------------------------------------- name editor
constexpr int NAME_SLOT_W = 22, NAME_SLOT_Y = 126;
constexpr int NAME_BTN_Y = 176;

int nameSlotX(int i) { return 120 - (NAME_LEN * (NAME_SLOT_W + 2)) / 2 + i * (NAME_SLOT_W + 2) + NAME_SLOT_W / 2; }

int charIndex(char ch) {
  const char *p = strchr(NAME_CHARS, ch);
  return p && ch ? p - NAME_CHARS : 0;
}

void openNameEditor(int player) {
  namePlayer = player;
  memset(nameBuf, ' ', NAME_LEN);
  nameBuf[NAME_LEN] = 0;
  if (mtg::hasCustomName(player)) memcpy(nameBuf, game.name[player], strlen(game.name[player]));
  int len = NAME_LEN;
  while (len > 0 && nameBuf[len - 1] == ' ') len--;
  nameCursor = len < NAME_LEN ? len : NAME_LEN - 1;
  nameFrom = screen == SCR_COLOR ? colorFrom : screen;  // return to where the picker was opened from
  screen = SCR_NAME;
  requestRedraw();
}

void closeNameEditor(bool save) {
  if (save) {
    char out[NAME_LEN + 1];
    memcpy(out, nameBuf, NAME_LEN + 1);
    int len = NAME_LEN;
    while (len > 0 && out[len - 1] == ' ') len--;  // trim trailing spaces
    int start = 0;
    while (start < len && out[start] == ' ') start++;  // and leading ones
    out[len] = 0;
    if (strcmp(out + start, game.name[namePlayer])) {
      mtg::markUndo();
      mtg::editGame();  // saved, undoable and shared with linked dials
      memset(game.name[namePlayer], 0, sizeof(game.name[namePlayer]));
      memcpy(game.name[namePlayer], out + start, len - start);
    }
  }
  if (nameChain) {  // new-game setup: next player, then the first-player spinner
    if (namePlayer + 1 < game.numPlayers) {
      openNameEditor(namePlayer + 1);
      return;
    }
    nameChain = 0;
    startSpinner();
    return;
  }
  screen = nameFrom;
  if (screen == SCR_PLAYER) buildRows();
  requestRedraw();
}

void handleName(const InputEvents &ev) {
  if (ev.detents) {
    char &ch = nameBuf[nameCursor];
    int d = ev.detents;
    if (ch == ' ') {  // empty slot: the first click picks a sensible starting letter
      bool afterLetter = nameCursor > 0 && isalpha((unsigned char)nameBuf[nameCursor - 1]);
      ch = afterLetter ? 'a' : 'A';
      d -= d > 0 ? 1 : -1;
    }
    int n = strlen(NAME_CHARS);
    ch = NAME_CHARS[((charIndex(ch) + d) % n + n) % n];
  }
  if (ev.click) nameCursor = min(nameCursor + 1, NAME_LEN - 1);
  if (ev.longPress) closeNameEditor(true);
  if (ev.tap) {
    if (abs(ev.y - NAME_SLOT_Y) <= 18) {
      for (int i = 0; i < NAME_LEN; i++)
        if (abs(ev.x - nameSlotX(i)) <= NAME_SLOT_W / 2 + 1) nameCursor = i;
    } else if (abs(ev.y - NAME_BTN_Y) <= 18 && ev.x < 120) {  // Del: remove the letter at the cursor
      memmove(nameBuf + nameCursor, nameBuf + nameCursor + 1, NAME_LEN - nameCursor - 1);
      nameBuf[NAME_LEN - 1] = ' ';
      if (nameCursor > 0 && nameBuf[nameCursor] == ' ') nameCursor--;
    } else if (abs(ev.y - NAME_BTN_Y) <= 18) {
      closeNameEditor(true);
    } else if (ev.y < 40) {
      closeNameEditor(false);
    }
  }
}

void drawNameEditor() {
  const Theme &th = T();
  uint32_t pc = playerColor(namePlayer);
  canvas.fillScreen(th.bg);
  canvas.setTextDatum(textdatum_t::middle_center);
  if (nameChain) {
    char t[24];
    snprintf(t, sizeof(t), "Player %d", namePlayer + 1);
    canvas.setFont(&fonts::DejaVu18);
    int w = canvas.textWidth(t);
    drawIcon(canvas, IC_PENCIL, 120 - w / 2 - 6, 26, 14, pc, th.bg);
    canvas.setTextColor(pc);
    canvas.drawString(t, 128, 27);
    snprintf(t, sizeof(t), "%d of %d", namePlayer + 1, game.numPlayers);
    canvas.setTextColor(th.muted);
    canvas.setFont(&fonts::DejaVu9);
    canvas.drawString(t, 120, 45);
  } else {
    drawIcon(canvas, IC_PENCIL, 92, 26, 14, pc, th.bg);
    canvas.setTextColor(pc);
    canvas.setFont(&fonts::DejaVu18);
    canvas.drawString("Name", 128, 27);
  }

  // Letter wheel for the slot under the cursor.
  int n = strlen(NAME_CHARS);
  int ci = charIndex(nameBuf[nameCursor]);
  for (int k = -2; k <= 2; k++) {
    char s[2] = {NAME_CHARS[((ci + k) % n + n) % n], 0};
    if (s[0] == ' ') s[0] = '_';
    canvas.setFont(k == 0 ? &fonts::DejaVu40 : &fonts::DejaVu18);
    canvas.setTextColor(k == 0 ? th.text : mix(th.muted, th.bg, abs(k) == 1 ? 0.2f : 0.55f));
    canvas.drawString(s, 120 + k * 36 + (k > 0 ? 6 : k < 0 ? -6 : 0), 76);
  }
  canvas.fillTriangle(114, 100, 126, 100, 120, 94, pc);

  // The 8 slots.
  canvas.setFont(&fonts::DejaVu18);
  for (int i = 0; i < NAME_LEN; i++) {
    int x = nameSlotX(i);
    bool cur = i == nameCursor;
    uint32_t box = cur ? pc : th.surface2;
    canvas.fillRoundRect(x - NAME_SLOT_W / 2, NAME_SLOT_Y - 15, NAME_SLOT_W, 30, 6, box);
    char s[2] = {nameBuf[i], 0};
    canvas.setTextColor(cur ? textOn(pc) : th.text);
    canvas.drawString(s, x, NAME_SLOT_Y + 1);
  }

  // Del / Done.
  canvas.fillRoundRect(42, NAME_BTN_Y - 15, 70, 30, 15, th.surface2);
  drawIcon(canvas, IC_BACK, 60, NAME_BTN_Y, 12, th.text, th.surface2);
  canvas.setTextColor(th.text);
  canvas.setFont(&fonts::DejaVu12);
  canvas.drawString("Del", 84, NAME_BTN_Y + 1);
  canvas.fillRoundRect(128, NAME_BTN_Y - 15, 70, 30, 15, pc);
  canvas.setTextColor(textOn(pc));
  canvas.drawString(!nameChain ? "Done" : namePlayer + 1 < game.numPlayers ? "Next" : "Start", 163, NAME_BTN_Y + 1);

  canvas.setTextColor(th.muted);
  canvas.setFont(&fonts::DejaVu9);
  canvas.drawString("turn: letter  press: next", 120, 206);
  canvas.drawString("hold: save   empty = default", 120, 218);
}

// ---------------------------------------------------------------- touch controls
int stepButtonY() { return screen == SCR_ATTACK ? 100 : 118; }

// -1 / +1 when (x, y) is on the editor's or attack screen's minus / plus button.
int stepButtonAt(int x, int y) {
  if (screen != SCR_EDIT && screen != SCR_ATTACK) return 0;
  if (abs(y - stepButtonY()) > STEP_R + 8) return 0;
  if (abs(x - 40) <= STEP_R + 8) return -1;
  if (abs(x - 200) <= STEP_R + 8) return 1;
  return 0;
}

void drawStepButtons(int y, uint32_t color) {
  const Theme &th = T();
  for (int k = 0; k < 2; k++) {
    int x = k ? 200 : 40;
    bool pressed = stepHolding && stepDir == (k ? 1 : -1);
    uint32_t fill = pressed ? color : th.surface2;
    uint32_t fg = pressed ? textOn(color) : color;
    canvas.fillCircle(x, y, STEP_R + 2, th.bg);
    canvas.fillCircle(x, y, STEP_R, fill);
    canvas.fillRoundRect(x - 10, y - 2, 20, 5, 2, fg);
    if (k) canvas.fillRoundRect(x - 2, y - 10, 5, 20, 2, fg);
  }
}

void editStep(int d, uint32_t now) {
  int lethal, before = rowValue(editRow, &lethal);
  adjustRow(editRow, d);
  int changed = rowValue(editRow, &lethal) - before;
  if (changed) {
    if (now - editDeltaAt > DELTA_SHOW_MS) editDelta = 0;
    editDelta += changed;
    editDeltaAt = now;
    redrawAt(now + DELTA_SHOW_MS);
  }
}

void applyStep(int d, uint32_t now) {
  if (screen == SCR_EDIT) editStep(d, now);
  else if (screen == SCR_ATTACK) attackAmount = constrain(attackAmount + d * mtg::lifeStep(), 0, 99 * mtg::lifeStep());
  requestRedraw();
}

void tilePos(int slot, int *x, int *y) {
  *x = 64 + (slot % 3) * 56;
  *y = 64 + (slot / 3) * 56;
}

int tilePages() { return max(1, ((int)rows.size() + TILES_PER_PAGE - 1) / TILES_PER_PAGE); }

void drawTiles() {
  const Theme &th = T();
  canvas.fillScreen(th.bg);
  int count = rows.size();
  int page = listSel / TILES_PER_PAGE;

  // Title: back chevron + player name (tap to go back, hold to recolour/rename).
  char name[24];
  mtg::playerName(focus, name, sizeof(name));
  canvas.setFont(&fonts::DejaVu18);
  int tw = canvas.textWidth(name) + 18;
  drawIcon(canvas, IC_BACK, 120 - tw / 2 + 5, 21, 12, th.muted, th.bg);
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.setTextColor(playerColor(focus));
  canvas.drawString(name, 120 - tw / 2 + 18, 22);
  canvas.setTextDatum(textdatum_t::middle_center);

  for (int slot = 0; slot < TILES_PER_PAGE; slot++) {
    int idx = page * TILES_PER_PAGE + slot;
    if (idx >= count) break;
    int id = rows[idx];
    int x, y;
    tilePos(slot, &x, &y);
    RowText r = rowText(id);
    int lethal = 0, v = 0;
    bool numeric = rowEditable(id);
    if (numeric) v = rowValue(id, &lethal);
    bool active = (id == R_MONARCH && game.monarch == focus) || (id == R_INITIATIVE && game.initiative == focus) ||
                  (id == R_CONCEDE && game.p[focus].conceded);
    bool dead = numeric && lethal && v >= lethal && id != R_COUNTER + C_SPEED && id != R_LIFE;
    uint32_t col = r.icon ? r.iconColor : r.labelColor;
    uint32_t fill = mix(col, th.bg, active ? 0.45f : (settings.theme ? 0.82f : 0.8f));

    if (idx == listSel) canvas.fillCircle(x, y, TILE_R + 3, th.text);  // dial highlight
    canvas.fillCircle(x, y, TILE_R + (idx == listSel ? 1 : 0), dead ? th.bad : fill);
    if (dead) canvas.fillCircle(x, y, TILE_R - 3, fill);
    drawIcon(canvas, r.icon ? r.icon : IC_MENU, x, y - 9, 18, col, fill);

    char cap[12];
    if (numeric) {
      if (id >= R_CMD && game.partners) snprintf(cap, sizeof(cap), "%c %d", (id - R_CMD) % 2 ? 'B' : 'A', v);
      else snprintf(cap, sizeof(cap), "%d", v);
    } else {
      const char *c = id == R_MONARCH ? "Monarch" : id == R_INITIATIVE ? "Initiat." : id == R_COLOR ? "Color"
                      : id == R_CONCEDE ? (game.p[focus].conceded ? "Rejoin" : "Concede")
                      : id == R_GAMEMENU ? "Menu" : id == R_BACK ? "Back" : "";
      if (id == R_NAME) c = mtg::hasCustomName(focus) ? game.name[focus] : "Name";
      snprintf(cap, sizeof(cap), "%s", c);
    }
    canvas.setTextColor(dead ? th.bad : th.text);
    canvas.setFont(numeric ? &fonts::DejaVu18 : &fonts::DejaVu9);
    if (canvas.textWidth(cap) > TILE_R * 2 - 6) canvas.setFont(&fonts::DejaVu12);
    if (canvas.textWidth(cap) > TILE_R * 2 - 6) canvas.setFont(&fonts::DejaVu9);
    canvas.drawString(cap, x, y + 11);
  }

  // Page dots (swipe left/right).
  int pages = tilePages();
  if (pages > 1) {
    for (int p = 0; p < pages; p++) {
      int dx = 120 + (p - (pages - 1) / 2.f) * 14;
      if (p == page) canvas.fillCircle(dx, 222, 4, th.accent);
      else canvas.fillCircle(dx, 222, 3, th.surface2);
    }
  }
}

void handleTiles(const InputEvents &ev) {
  int count = rows.size();
  if (!count) return;
  if (ev.detents) listSel = constrain(listSel + ev.detents, 0, count - 1);
  if (ev.click) pressRow(rows[listSel]);
  if (ev.longPress) openTable();
  if (ev.dragEnd) {  // swipe between pages
    int dx = ev.x - ev.sx, dy = ev.y - ev.sy;
    if (abs(dx) > 35 && abs(dx) > abs(dy)) {
      int page = listSel / TILES_PER_PAGE + (dx < 0 ? 1 : -1);
      page = constrain(page, 0, tilePages() - 1);
      listSel = min(page * TILES_PER_PAGE, count - 1);
    }
  }
  if (ev.tap) {
    if (ev.y < 40) {
      openTable();
      return;
    }
    if (ev.y > 210 && tilePages() > 1) {  // tap the dots side to page
      int page = constrain(listSel / TILES_PER_PAGE + (ev.x > 120 ? 1 : -1), 0, tilePages() - 1);
      listSel = min(page * TILES_PER_PAGE, count - 1);
      return;
    }
    int page = listSel / TILES_PER_PAGE;
    for (int slot = 0; slot < TILES_PER_PAGE; slot++) {
      int idx = page * TILES_PER_PAGE + slot;
      if (idx >= count) break;
      int x, y;
      tilePos(slot, &x, &y);
      if ((ev.x - x) * (ev.x - x) + (ev.y - y) * (ev.y - y) <= (TILE_R + 4) * (TILE_R + 4)) {
        listSel = idx;
        pressRow(rows[idx]);
        return;
      }
    }
  }
}


// ---------------------------------------------------------------- splash and game picker
// Shown at boot (with Resume) and after the 3 s reset: pick the card game (and the Magic
// format), the number of players, then standard or custom names.
constexpr uint32_t SPLASH_INTRO_MS = 1100;
enum SplashStep { SP_GAME, SP_PLAYERS, SP_NAMES, SP_MAGIC };
const uint8_t MAGIC_FORMATS[4] = {FMT_COMMANDER, FMT_STANDARD, FMT_ARCHENEMY, FMT_EMPEROR};
const uint8_t SYSTEM_FORMAT[SYS_COUNT] = {FMT_COMMANDER, FMT_YUGIOH, FMT_LORCANA, FMT_POKEMON, FMT_RIFTBOUND};
const Icon SYSTEM_ICONS[SYS_COUNT] = {IC_NONE, IC_STAR, IC_LORE, IC_PRIZE, IC_TROPHY};
const uint32_t SYSTEM_COLORS[SYS_COUNT] = {0xF76B15, 0xB07CF0, 0x5B8DEF, 0xEEC62B, 0x12A594};
const Icon FORMAT_ICONS[FMT_COUNT] = {IC_CARDS, IC_COMMANDER, IC_ARCHENEMY, IC_EMPEROR,
                                      IC_STAR, IC_LORE, IC_PRIZE, IC_TROPHY, IC_TROPHY};
const uint32_t FORMAT_COLORS[FMT_COUNT] = {0x3E8EF7, 0xF5A524, 0xE5484D, 0x8E4EC6,
                                           0xB07CF0, 0x5B8DEF, 0xEEC62B, 0x12A594, 0x12A594};
const uint32_t MANA[5] = {0xF6EFD2, 0x2F7FD0, 0x7A6A64, 0xE0412F, 0x2E9E5B};   // W U B R G
const uint32_t MANA_LED[5] = {0xFFE0A0, 0x0050FF, 0x7000C0, 0xFF1800, 0x00C030};
constexpr int SPLASH_DICE = SYS_COUNT;        // the cell after the five games
constexpr int SPLASH_RESUME = SYS_COUNT + 1;  // the slim button underneath
constexpr uint32_t DICE_COLOR = 0xE5484D;
constexpr int SPLASH_CHIP_Y = 116, SPLASH_START_Y = 176;

bool splashResume = false;
int splashStep = SP_GAME;
int splashSel = 0;       // game screen: 0..4 games, 5 = resume
int splashMagicSel = 0;  // Magic format screen: 0..3
int splashOpt = 0;       // player-count chip
int splashNames = 0;     // names screen: 0 = standard, 1 = custom
uint8_t splashFormat = FMT_COMMANDER;
int splashPlayers = 4;
uint32_t splashStart = 0;

struct PlayerOpt {
  uint8_t players, format;
};

// Player-count chips for the chosen game: 2..max, plus a 2v2 chip for Riftbound.
int playerOpts(PlayerOpt *o) {
  uint8_t f = splashFormat == FMT_RIFT_2V2 ? FMT_RIFTBOUND : splashFormat;
  int n = 0;
  for (int p = max(2, mtg::minPlayers(f)); p <= mtg::maxPlayers(f); p++) o[n++] = {(uint8_t)p, f};
  if (f == FMT_RIFTBOUND) o[n++] = {4, FMT_RIFT_2V2};
  return n;
}

void openSplash(bool resume) {
  splashResume = resume;
  splashStep = SP_GAME;
  nameChain = 0;
  splashSel = mtg::systemOf(settings.format);
  if (resume) splashSel = SPLASH_RESUME;
  splashStart = millis();
  holdShownMs = 0;
  screen = SCR_SPLASH;
  requestRedraw();
}

// 2 x 3 grid on the game screen, 2 x 2 on the Magic format screen.
void splashCell(int k, bool magic, int *x, int *y, int *w, int *h) {
  *w = 80;
  *x = (k % 2) ? 122 : 38;
  if (magic) {
    *h = 54;
    *y = (k / 2) ? 120 : 62;
  } else if (k == SPLASH_RESUME) {
    *x = 68;
    *w = 104;
    *h = 26;
    *y = 190;
  } else {
    *h = 42;
    *y = 48 + (k / 2) * 46;
  }
}

void startFromSplash(bool customNames) {
  settings.format = splashFormat;
  settings.numPlayers = splashPlayers;
  settings.startLife = mtg::defaultLife(splashFormat);
  mtg::markSettingsDirty();
  if (!customNames) memset(game.name, 0, sizeof(game.name));  // Player 1, Player 2 ... (newGame keeps names)
  mtg::newGame();  // broadcast: linked dials follow into the spinner
  wasOver = false;
  focus = 0;
  if (!customNames) {
    startSpinner();
    return;
  }
  seenGameId = game.gameId;  // our own new game: don't let the link sync jump to the table
  seenFirst = game.firstPlayer;
  nameChain = 1;
  openNameEditor(0);
  nameFrom = SCR_TABLE;
}

void commitPlayerOpt() {
  PlayerOpt o[8];
  int n = playerOpts(o);
  splashOpt = constrain(splashOpt, 0, n - 1);
  splashFormat = o[splashOpt].format;
  splashPlayers = o[splashOpt].players;
}

// Format picked: show the player count, or go straight to names when there's only one choice.
void enterPlayers() {
  PlayerOpt o[8];
  int n = playerOpts(o);
  splashOpt = 0;
  int want = splashFormat == game.format ? game.numPlayers : mtg::defaultPlayers(splashFormat);
  for (int k = 0; k < n; k++)
    if (o[k].players == want && (o[k].format == splashFormat || splashFormat != game.format)) {
      splashOpt = k;
      if (o[k].format == game.format) break;
    }
  commitPlayerOpt();
  splashStep = n > 1 ? SP_PLAYERS : SP_NAMES;
}

void splashBack() {
  PlayerOpt o[8];
  bool magic = mtg::systemOf(splashFormat) == SYS_MAGIC;
  if (splashStep == SP_NAMES && playerOpts(o) > 1) splashStep = SP_PLAYERS;
  else if (splashStep != SP_MAGIC && magic) splashStep = SP_MAGIC;
  else splashStep = SP_GAME;
}

void chooseGame(int s) {
  if (s == SYS_MAGIC) {
    splashMagicSel = 0;
    for (int k = 0; k < 4; k++)
      if (MAGIC_FORMATS[k] == settings.format) splashMagicSel = k;
    splashStep = SP_MAGIC;
    return;
  }
  splashFormat = SYSTEM_FORMAT[s];
  enterPlayers();
}

int splashChipX(int k, int n) { return 120 + (int)((k - (n - 1) / 2.f) * 38); }

bool inCell(const InputEvents &ev, int k, bool magic) {
  int x, y, w, h;
  splashCell(k, magic, &x, &y, &w, &h);
  return ev.x >= x && ev.x < x + w && ev.y >= y && ev.y < y + h;
}

void handleSplash(const InputEvents &ev, uint32_t now) {
  if (now - splashStart < SPLASH_INTRO_MS) {  // any input skips the intro
    if (ev.click || ev.tap || ev.detents) splashStart = now - SPLASH_INTRO_MS;
    return;
  }
  if (splashStep == SP_GAME) {
    int opts = splashResume ? SPLASH_RESUME + 1 : SPLASH_DICE + 1;
    if (ev.detents) splashSel = ((splashSel + ev.detents) % opts + opts) % opts;
    if (ev.click) {
      if (splashSel == SPLASH_RESUME) openTable();
      else if (splashSel == SPLASH_DICE) openDice();
      else chooseGame(splashSel);
    }
    if (ev.tap) {
      for (int k = 0; k < SYS_COUNT; k++)
        if (inCell(ev, k, false)) {
          splashSel = k;
          chooseGame(k);
          return;
        }
      if (inCell(ev, SPLASH_DICE, false)) {
        splashSel = SPLASH_DICE;
        openDice();
      } else if (splashResume && inCell(ev, SPLASH_RESUME, false)) {
        openTable();
      }
    }
    return;
  }
  if (splashStep == SP_MAGIC) {
    if (ev.detents) splashMagicSel = ((splashMagicSel + ev.detents) % 4 + 4) % 4;
    if (ev.click) {
      splashFormat = MAGIC_FORMATS[splashMagicSel];
      enterPlayers();
    }
    if (ev.longPress) splashBack();
    if (ev.tap) {
      for (int k = 0; k < 4; k++)
        if (inCell(ev, k, true)) {
          splashMagicSel = k;
          splashFormat = MAGIC_FORMATS[k];
          enterPlayers();
          return;
        }
      if (ev.y < 56 || ev.y > 186) splashBack();
    }
    return;
  }
  if (splashStep == SP_NAMES) {  // standard or custom names
    if (ev.detents) splashNames = ((splashNames + ev.detents) % 2 + 2) % 2;
    if (ev.click) startFromSplash(splashNames == 1);
    if (ev.longPress) splashBack();
    if (ev.tap) {
      if (abs(ev.x - 120) <= 84 && ev.y >= 76 && ev.y < 128) startFromSplash(false);
      else if (abs(ev.x - 120) <= 84 && ev.y >= 134 && ev.y < 186) startFromSplash(true);
      else if (ev.y < 70 || ev.y > 200) splashBack();
    }
    return;
  }
  // Player count
  PlayerOpt o[8];
  int n = playerOpts(o);
  if (ev.detents) {
    splashOpt = constrain(splashOpt + ev.detents, 0, n - 1);
    commitPlayerOpt();
  }
  if (ev.click) {
    splashStep = SP_NAMES;
    return;
  }
  if (ev.longPress) {
    splashBack();
    return;
  }
  if (ev.tap) {
    if (abs(ev.y - SPLASH_CHIP_Y) <= 22) {
      for (int k = 0; k < n; k++)
        if (abs(ev.x - splashChipX(k, n)) <= 19) splashOpt = k;
      commitPlayerOpt();
    } else if (abs(ev.y - (SPLASH_START_Y + 16)) <= 20 && abs(ev.x - 120) <= 64) {
      splashStep = SP_NAMES;
    } else if (ev.y < 74 || ev.y > 208) {
      splashBack();  // title or back arrow
    }
  }
}

// Five mana dots on a small ring: the Magic button's icon.
void drawManaRing(int cx, int cy, float r, float dot, float fade) {
  const Theme &th = T();
  for (int i = 0; i < 5; i++) {
    float a = (i * 72.f - 90.f) * DEG_TO_RAD;
    canvas.fillCircle(cx + cosf(a) * r, cy + sinf(a) * r, dot, mix(th.bg, MANA[i], fade));
  }
}

void drawSplashHeader(Icon ic, const char *title, uint32_t fc) {
  const Theme &th = T();
  canvas.fillArc(120, 120, 114, 119, 0, 360, mix(th.bg, fc, 0.35f));
  if (ic == IC_NONE) drawManaRing(120, 34, 7, 3, 1);
  else drawIcon(canvas, ic, 120, 34, 22, fc, th.bg);
  canvas.setTextColor(th.text);
  drawStringFit(canvas, title, 120, 60, 170, &fonts::DejaVu18, &fonts::DejaVu12);
}

const char *goalText(uint8_t f, int players, char *buf, int len) {
  int sl = mtg::defaultLife(f);
  switch (f) {
    case FMT_ARCHENEMY: snprintf(buf, len, "Archenemy %d, heroes %d", sl + 20, sl); break;
    case FMT_YUGIOH: snprintf(buf, len, "%d LP each", sl); break;
    case FMT_LORCANA: snprintf(buf, len, "first to 20 lore wins"); break;
    case FMT_POKEMON: snprintf(buf, len, "6 prize cards each"); break;
    case FMT_RIFTBOUND: snprintf(buf, len, "first to 8 points wins"); break;
    case FMT_RIFT_2V2: snprintf(buf, len, "teams of 2, first to 11"); break;
    default: snprintf(buf, len, "%d life each", sl);
  }
  return buf;
}

void drawSplash(uint32_t now) {
  const Theme &th = T();
  canvas.fillScreen(th.bg);
  canvas.setTextDatum(textdatum_t::middle_center);
  uint32_t t = now - splashStart;
  uint8_t sys = mtg::systemOf(splashFormat);

  if (splashStep == SP_NAMES) {
    uint32_t fc = FORMAT_COLORS[splashFormat];
    drawSplashHeader(IC_PENCIL, "Player names", fc);
    char sub[32];
    snprintf(sub, sizeof(sub), "%s, %d players", mtg::formatName(splashFormat), splashPlayers);
    canvas.setTextColor(th.muted);
    canvas.setFont(&fonts::DejaVu9);
    canvas.drawString(sub, 120, 76);
    for (int k = 0; k < 2; k++) {
      int y = k ? 136 : 86;
      bool sel = splashNames == k;
      uint32_t fill = sel ? fc : th.surface2;
      uint32_t fg = sel ? textOn(fc) : th.text;
      canvas.fillRoundRect(40, y, 160, 44, 16, fill);
      drawIcon(canvas, k ? IC_PENCIL : IC_PLAYERS, 62, y + 22, 18, fg, fill);
      canvas.setTextDatum(textdatum_t::middle_left);
      canvas.setTextColor(fg);
      canvas.setFont(&fonts::DejaVu12);
      canvas.drawString(k ? "Custom" : "Standard", 80, y + 15);
      char d[24];
      if (k) snprintf(d, sizeof(d), "type each name");
      else snprintf(d, sizeof(d), "Player 1 - %d", splashPlayers);
      canvas.setFont(&fonts::DejaVu9);
      canvas.setTextColor(sel ? fg : th.muted);
      canvas.drawString(d, 80, y + 31);
      canvas.setTextDatum(textdatum_t::middle_center);
    }
    canvas.setTextColor(th.muted);
    canvas.setFont(&fonts::DejaVu9);
    canvas.drawString("press or tap to start", 120, 196);
    drawIcon(canvas, IC_BACK, 120, 222, 14, th.muted, th.bg);
    return;
  }
  if (splashStep == SP_PLAYERS) {
    uint32_t fc = FORMAT_COLORS[splashFormat];
    drawSplashHeader(sys == SYS_MAGIC ? FORMAT_ICONS[splashFormat] : SYSTEM_ICONS[sys],
                     sys == SYS_MAGIC ? mtg::formatName(splashFormat) : mtg::systemName(sys), fc);
    canvas.setTextColor(th.muted);
    canvas.setFont(&fonts::DejaVu9);
    canvas.drawString("How many players?", 120, 86);
    PlayerOpt o[8];
    int n = playerOpts(o);
    for (int k = 0; k < n; k++) {
      int x = splashChipX(k, n);
      bool on = k == splashOpt;
      canvas.fillCircle(x, SPLASH_CHIP_Y, 17, on ? fc : th.surface2);
      canvas.setTextColor(on ? textOn(fc) : th.text);
      char b[6];
      if (o[k].format == FMT_RIFT_2V2) {
        snprintf(b, sizeof(b), "2v2");
        canvas.setFont(&fonts::DejaVu12);
      } else {
        snprintf(b, sizeof(b), "%d", o[k].players);
        canvas.setFont(&fonts::DejaVu18);
      }
      canvas.drawString(b, x, SPLASH_CHIP_Y + 1);
    }
    char life[32];
    canvas.setTextColor(th.muted);
    canvas.setFont(&fonts::DejaVu9);
    canvas.drawString(goalText(splashFormat, splashPlayers, life, sizeof(life)), 120, 150);
    canvas.fillRoundRect(60, SPLASH_START_Y, 120, 32, 16, fc);
    uint32_t on = textOn(fc);
    drawIcon(canvas, IC_PLAY, 96, SPLASH_START_Y + 16, 14, on, fc);
    canvas.setTextColor(on);
    canvas.setFont(&fonts::DejaVu18);
    canvas.drawString("Next", 128, SPLASH_START_Y + 17);
    drawIcon(canvas, IC_BACK, 120, 222, 14, th.muted, th.bg);
    return;
  }
  if (splashStep == SP_MAGIC) {
    canvas.fillArc(120, 120, 114, 119, 0, 360, mix(th.bg, SYSTEM_COLORS[SYS_MAGIC], 0.35f));
    for (int i = 0; i < 5; i++) canvas.fillCircle(120 + (i - 2) * 14, 22, 4, MANA[i]);
    canvas.setTextColor(th.text);
    canvas.setFont(&fonts::DejaVu18);
    canvas.drawString("Magic", 120, 44);
    for (int k = 0; k < 4; k++) {
      int x, y, w, h;
      splashCell(k, true, &x, &y, &w, &h);
      uint8_t f = MAGIC_FORMATS[k];
      uint32_t fc = FORMAT_COLORS[f];
      bool sel = splashMagicSel == k;
      uint32_t cardBg = sel ? mix(th.surface, fc, 0.25f) : th.surface;
      canvas.fillRoundRect(x, y, w, h, 14, cardBg);
      if (sel) {
        canvas.drawRoundRect(x, y, w, h, 14, fc);
        canvas.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 13, fc);
      }
      drawIcon(canvas, FORMAT_ICONS[f], x + w / 2, y + 16, 18, fc, cardBg);
      canvas.setTextColor(th.text);
      canvas.setFont(&fonts::DejaVu12);
      canvas.drawString(mtg::formatName(f), x + w / 2, y + 34);
      char d[16];
      if (f == FMT_EMPEROR) snprintf(d, sizeof(d), "6p teams");
      else snprintf(d, sizeof(d), "%d life", mtg::defaultLife(f) + (f == FMT_ARCHENEMY ? 20 : 0));
      canvas.setTextColor(th.muted);
      canvas.setFont(&fonts::DejaVu9);
      canvas.drawString(d, x + w / 2, y + 47);
    }
    drawIcon(canvas, IC_BACK, 120, 204, 14, th.muted, th.bg);
    return;
  }

  // Game screen. Logo: the five mana colours spiral out of the centre, then the title fades in.
  auto ease = [](float x) { x = constrain(x, 0.f, 1.f); return 1.f - powf(1.f - x, 3.f); };
  float pd = ease(t / 700.f);
  for (int i = 0; i < 5; i++) {
    float a = (i * 72.f - 90.f + (1 - pd) * 300.f) * DEG_TO_RAD;
    float r0 = 46.f * (1 - pd) + 10.f;
    float sx = 120 + cosf(a) * r0, sy = 120 + sinf(a) * r0;
    float ex = 120 + (i - 2) * 14, ey = 13;
    float x = sx + (ex - sx) * pd, y = sy + (ey - sy) * pd;
    float rad = 4 + (1 - pd) * 5;
    canvas.fillCircle(x, y, rad, MANA[i]);
    if (settings.theme && i == 0) canvas.drawCircle(x, y, rad, th.muted);
  }
  float pt = ease((t - 300) / 500.f);
  canvas.setTextColor(mix(th.bg, th.text, pt));
  canvas.setFont(&fonts::DejaVu18);
  canvas.drawString("Duel Dials!", 120, 31);

  float pb = ease((t - 550) / 500.f);
  if (pb <= 0) return;
  int cells = splashResume ? SPLASH_RESUME + 1 : SPLASH_DICE + 1;
  for (int k = 0; k < cells; k++) {
    int x, y, w, h;
    splashCell(k, false, &x, &y, &w, &h);
    y += (int)((1 - pb) * 14);
    bool sel = splashSel == k && pb >= 1;
    bool resume = k == SPLASH_RESUME;
    bool dice = k == SPLASH_DICE;
    uint32_t fc = resume ? th.accent : dice ? DICE_COLOR : SYSTEM_COLORS[k];
    uint32_t cardBg = resume && sel ? th.accent : resume ? th.surface2 : sel ? mix(th.surface, fc, 0.25f) : th.surface;
    canvas.fillRoundRect(x, y, w, h, resume ? 13 : 14, mix(th.bg, cardBg, pb));
    if (sel && !resume) {
      canvas.drawRoundRect(x, y, w, h, 14, fc);
      canvas.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 13, fc);
    }
    if (resume) {
      uint32_t fg = mix(th.bg, sel ? textOn(th.accent) : th.text, pb);
      canvas.setFont(&fonts::DejaVu12);
      int tw = canvas.textWidth("Resume");
      drawIcon(canvas, IC_PLAY, x + w / 2 - tw / 2 - 4, y + 13, 10, fg, cardBg);
      canvas.setTextColor(fg);
      canvas.drawString("Resume", x + w / 2 + 6, y + 14);
      continue;
    }
    if (dice) {
      drawIcon(canvas, IC_D20, x + w / 2, y + 14, 18, mix(th.bg, fc, pb), cardBg);
      canvas.setTextColor(mix(th.bg, th.text, pb));
      canvas.setFont(&fonts::DejaVu12);
      canvas.drawString("DND Dice", x + w / 2, y + 32);
      continue;
    }
    if (SYSTEM_ICONS[k] == IC_NONE) drawManaRing(x + w / 2, y + 14, 6.5f, 2.6f, pb);
    else drawIcon(canvas, SYSTEM_ICONS[k], x + w / 2, y + 14, 18, mix(th.bg, fc, pb), cardBg);
    canvas.setTextColor(mix(th.bg, th.text, pb));
    canvas.setFont(&fonts::DejaVu12);
    canvas.drawString(mtg::systemName(k), x + w / 2, y + 32);
  }
}


// ---------------------------------------------------------------- DND dice
// d4 to d100 around the rim; turn to pick, press or tap the die to roll. Local to this dial.
constexpr int DIE_COUNT = 6;
const int DIE_SIDES[DIE_COUNT] = {4, 6, 8, 10, 20, 100};
const uint32_t DIE_COLORS[DIE_COUNT] = {0xE5484D, 0xF76B15, 0xEEC62B, 0x30A46C, 0x5B5BD6, 0xD23FBF};
const float DIE_ANGLES[DIE_COUNT] = {-120, -60, 0, 60, 120, 180};  // clockwise from upper left
constexpr int DIE_CHIP_R = 19, DIE_RING_R = 92;
constexpr uint32_t ROLL_MS = 1100;
constexpr int DICE_HISTORY = 6;

int dieSel = 4;  // d20
int dieResult = 0, dieShown = 0;
bool dieRolling = false;
uint32_t rollStart = 0, rollLastFlip = 0;
float rollSpin = 0;
int16_t rollHistory[DICE_HISTORY];
uint8_t rollHistorySides[DICE_HISTORY];
int rollHistoryCount = 0;

void openDice() {
  dieRolling = false;
  dieResult = dieShown = 0;
  rollSpin = 0;
  screen = SCR_DICE;
  requestRedraw();
}

void rollDie() {
  dieResult = esp_random() % DIE_SIDES[dieSel] + 1;
  dieShown = esp_random() % DIE_SIDES[dieSel] + 1;
  dieRolling = true;
  rollStart = rollLastFlip = millis();
  requestRedraw();
}

void finishRoll() {
  dieRolling = false;
  dieShown = dieResult;
  memmove(rollHistory + 1, rollHistory, sizeof(rollHistory[0]) * (DICE_HISTORY - 1));
  memmove(rollHistorySides + 1, rollHistorySides, DICE_HISTORY - 1);
  rollHistory[0] = dieResult;
  rollHistorySides[0] = dieSel;
  rollHistoryCount = min(rollHistoryCount + 1, DICE_HISTORY);
  requestRedraw();
}

void diceChip(int k, int *x, int *y) {
  float a = DIE_ANGLES[k] * DEG_TO_RAD;
  *x = 120 + (int)(cosf(a) * DIE_RING_R);
  *y = 120 + (int)(sinf(a) * DIE_RING_R);
}

void goHome() {
  editing = false;
  dragActive = false;
  stepHolding = false;
  holdConsumed = false;
  holdShownMs = 0;
  bool wasDice = screen == SCR_DICE;
  openSplash(true);
  if (wasDice) splashSel = SPLASH_DICE;
  splashStart = millis() - SPLASH_INTRO_MS;  // straight to the tiles
}

void closeDice() {
  openSplash(splashResume);
  splashSel = SPLASH_DICE;
  splashStart = millis() - SPLASH_INTRO_MS;  // no intro on the way back
}

void handleDice(const InputEvents &ev, uint32_t now) {
  if (ev.detents && !dieRolling) {
    dieSel = ((dieSel + ev.detents) % DIE_COUNT + DIE_COUNT) % DIE_COUNT;
    dieResult = dieShown = 0;
  }
  if (ev.click) {
    if (dieRolling) finishRoll();
    else rollDie();
  }
  if (ev.longPress) {
    closeDice();
    return;
  }
  if (ev.tap) {
    for (int k = 0; k < DIE_COUNT; k++) {
      int x, y;
      diceChip(k, &x, &y);
      if (abs(ev.x - x) <= DIE_CHIP_R + 4 && abs(ev.y - y) <= DIE_CHIP_R + 4) {
        if (!dieRolling) {
          dieSel = k;
          dieResult = dieShown = 0;
        }
        return;
      }
    }
    if (ev.y > 196 && abs(ev.x - 120) < 30) {
      closeDice();  // back arrow
      return;
    }
    float dx = ev.x - 120, dy = ev.y - 116;
    if (dx * dx + dy * dy < 66 * 66) {
      if (dieRolling) finishRoll();
      else rollDie();
    }
  }
}

// The die's outline: d4 triangle, d6 square, d8 diamond, d10 kite, d20 hexagon with a face, d100 two tens.
void drawDieShape(int k, int cx, int cy, float r, float rot, uint32_t fill, uint32_t edge) {
  static const int VERTS[DIE_COUNT] = {3, 4, 4, 5, 6, 10};
  int n = VERTS[k];
  float base = rot - 90 + (k == 1 ? 45 : 0);
  float px[10], py[10];
  for (int i = 0; i < n; i++) {
    float a = (base + i * 360.f / n) * DEG_TO_RAD;
    float rr = r;
    if (k == 0) rr = r * 1.12f;                 // a fuller triangle
    if (k == 2 && i % 2) rr = r * 0.78f;        // diamond: narrower sides
    if (k == 3 && (i == 1 || i == 4)) rr = r * 0.72f;  // kite-ish d10
    px[i] = cx + cosf(a) * rr;
    py[i] = cy + sinf(a) * rr;
  }
  for (int i = 0; i < n; i++) canvas.fillTriangle(cx, cy, px[i], py[i], px[(i + 1) % n], py[(i + 1) % n], fill);
  for (int i = 0; i < n; i++) canvas.drawWideLine(px[i], py[i], px[(i + 1) % n], py[(i + 1) % n], 3.f, edge);
  if (k == 4) {  // the d20's front face
    float fr = r * 0.52f;
    float fx[3], fy[3];
    for (int i = 0; i < 3; i++) {
      float a = (rot - 90 + i * 120) * DEG_TO_RAD;
      fx[i] = cx + cosf(a) * fr;
      fy[i] = cy + sinf(a) * fr;
    }
    for (int i = 0; i < 3; i++) {
      canvas.drawWideLine(fx[i], fy[i], fx[(i + 1) % 3], fy[(i + 1) % 3], 1.6f, mix(fill, edge, 0.6f));
      canvas.drawWideLine(fx[i], fy[i], px[(2 * i) % 6], py[(2 * i) % 6], 1.6f, mix(fill, edge, 0.6f));
    }
  }
}

void drawDice(uint32_t now) {
  const Theme &th = T();
  canvas.fillScreen(th.bg);
  canvas.setTextDatum(textdatum_t::middle_center);
  int sides = DIE_SIDES[dieSel];
  uint32_t dc = DIE_COLORS[dieSel];
  bool done = !dieRolling && dieResult > 0;
  bool crit = done && sides == 20 && dieResult == 20;
  bool fumble = done && sides == 20 && dieResult == 1;

  // Selector chips around the rim.
  for (int k = 0; k < DIE_COUNT; k++) {
    int x, y;
    diceChip(k, &x, &y);
    bool on = k == dieSel;
    uint32_t c = DIE_COLORS[k];
    canvas.fillCircle(x, y, DIE_CHIP_R, on ? c : mix(c, th.bg, settings.theme ? 0.78f : 0.75f));
    char b[6];
    snprintf(b, sizeof(b), "d%d", DIE_SIDES[k]);
    canvas.setFont(DIE_SIDES[k] == 100 ? &fonts::DejaVu9 : &fonts::DejaVu12);
    canvas.setTextColor(on ? textOn(c) : th.text);
    canvas.drawString(b, x, y + 1);
  }

  // Last rolls across the top.
  if (rollHistoryCount) {
    char h[40] = "";
    int len = 0;
    for (int i = 0; i < rollHistoryCount && i < 4; i++)
      len += snprintf(h + len, sizeof(h) - len, i ? "  %d" : "%d", rollHistory[i]);
    canvas.setFont(&fonts::DejaVu9);
    canvas.setTextColor(th.muted);
    canvas.drawString(h, 120, 14);
  } else {
    canvas.setFont(&fonts::DejaVu9);
    canvas.setTextColor(th.muted);
    canvas.drawString("DND Dice", 120, 14);
  }

  // The die, tumbling while it rolls.
  float t = dieRolling ? min(1.f, (now - rollStart) / (float)ROLL_MS) : 1.f;
  float wobble = dieRolling ? (1 - t) * 540.f : 0;
  float pop = done && now - rollStart < ROLL_MS + 250 ? 1.f + 0.12f * sinf((now - rollStart - ROLL_MS) / 250.f * PI) : 1.f;
  uint32_t edge = crit ? th.gold : fumble ? th.bad : dc;
  uint32_t fill = mix(dc, th.bg, settings.theme ? 0.8f : 0.72f);
  if (crit) fill = mix(th.gold, th.bg, 0.65f);
  drawDieShape(dieSel, 120, 114, 56 * pop, wobble, fill, edge);

  char num[8];
  if (dieShown) snprintf(num, sizeof(num), "%d", dieShown);
  else snprintf(num, sizeof(num), "d%d", sides);
  canvas.setTextColor(dieShown ? th.text : textOn(fill) == 0xFFFFFF ? th.text : th.text);
  drawStringFit(canvas, num, 120, 116, dieSel == 0 ? 52 : 80, &fonts::DejaVu56, &fonts::DejaVu40);

  canvas.setFont(&fonts::DejaVu12);
  if (crit) {
    canvas.setTextColor(th.gold);
    canvas.drawString("Natural 20!", 120, 183);
  } else if (fumble) {
    canvas.setTextColor(th.bad);
    canvas.drawString("Critical fail", 120, 183);
  } else if (done) {
    snprintf(num, sizeof(num), "d%d", sides);
    canvas.setTextColor(dc);
    canvas.drawString(num, 120, 183);
  } else if (!dieRolling) {
    canvas.setTextColor(th.muted);
    canvas.setFont(&fonts::DejaVu9);
    canvas.drawString("tap or press to roll", 120, 183);
  }
  drawIcon(canvas, IC_BACK, 120, 214, 14, th.muted, th.bg);
}

void updateDice(uint32_t now) {
  if (screen != SCR_DICE) return;
  if (dieRolling) {
    float t = (now - rollStart) / (float)ROLL_MS;
    if (t >= 1) {
      finishRoll();
      return;
    }
    uint32_t gap = 40 + (uint32_t)(t * t * 220);  // numbers flick fast, then slow down
    if (now - rollLastFlip >= gap) {
      rollLastFlip = now;
      int s = DIE_SIDES[dieSel];
      int v;
      do v = esp_random() % s + 1; while (s > 1 && v == dieShown);
      dieShown = v;
    }
    requestRedraw();
  } else if (dieResult && now - rollStart < ROLL_MS + 300) {
    requestRedraw();  // landing pop
  }
}


// ---------------------------------------------------------------- connectivity
// Quick screen for linking dials: the room (applied on Done, so passing through a room
// doesn't join it) and the seat this dial belongs to (applied straight away).
int connRoom = 0;
int connRow = 0;  // 0 = room, 1 = seat
constexpr int CONN_ROOM_Y = 76, CONN_SEAT_Y = 126, CONN_BTN_R = 17, CONN_DONE_Y = 200;
bool phoneQrUrl = false;  // phone screen: false = join-Wi-Fi QR, true = app address QR
void openPhone() {
  phoneQrUrl = false;
  screen = SCR_PHONE;
  requestRedraw();
}

void openConnect() {
  connRoom = settings.linkRoom;
  connRow = 0;
  editing = false;
  screen = SCR_CONNECT;
  requestRedraw();
}

void closeConnect() {
  if (connRoom != settings.linkRoom) {
    settings.linkRoom = connRoom;
    net::setRoom(settings.linkRoom);
    mtg::markSettingsDirty();
    toast(connRoom ? "Joining room" : "Link off");
  }
  openMenu();
  for (int i = 0; i < (int)rows.size(); i++)
    if (rows[i] == M_LINK) listSel = i;
}

void connAdjust(int row, int d) {
  if (row == 0) {
    connRoom = constrain(connRoom + d, 0, 9);
  } else {
    settings.mySeat = constrain((int)settings.mySeat + d, 0, (int)game.numPlayers);
    if (settings.mySeat) focus = settings.mySeat - 1;
    mtg::markSettingsDirty();
  }
}

void handleConnect(const InputEvents &ev) {
  if (ev.detents) connAdjust(connRow, ev.detents);
  if (ev.click) connRow = !connRow;
  if (ev.longPress) {
    closeConnect();
    return;
  }
  if (!ev.tap) return;
  for (int row = 0; row < 2; row++) {
    int y = row ? CONN_SEAT_Y : CONN_ROOM_Y;
    if (abs(ev.y - y) > 22) continue;
    connRow = row;
    if (abs(ev.x - 50) <= CONN_BTN_R + 8) connAdjust(row, -1);
    else if (abs(ev.x - 190) <= CONN_BTN_R + 8) connAdjust(row, 1);
    return;
  }
  if (abs(ev.y - CONN_DONE_Y) <= 16 && ev.x < 120 && ev.x > 40) openPhone();
  else if (ev.y < 44 || (abs(ev.y - CONN_DONE_Y) <= 16 && ev.x >= 120 && ev.x < 200)) closeConnect();
}

void drawConnect() {
  const Theme &th = T();
  canvas.fillScreen(th.bg);
  canvas.setTextDatum(textdatum_t::middle_center);
  bool on = connRoom > 0;

  // Title
  canvas.setFont(&fonts::DejaVu18);
  int tw = canvas.textWidth("Connectivity") + 22;
  drawIcon(canvas, IC_LINK, 120 - tw / 2 + 8, 30, 16, on ? th.accent : th.muted, th.bg);
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.setTextColor(th.text);
  canvas.drawString("Connectivity", 120 - tw / 2 + 22, 31);
  canvas.setTextDatum(textdatum_t::middle_center);

  // Two rows: label above, - value + across.
  for (int row = 0; row < 2; row++) {
    int y = row ? CONN_SEAT_Y : CONN_ROOM_Y;
    bool sel = connRow == row;
    uint32_t col = row ? (settings.mySeat ? playerColor(settings.mySeat - 1) : th.muted) : (on ? th.accent : th.muted);
    canvas.setFont(&fonts::DejaVu9);
    canvas.setTextColor(sel ? th.text : th.muted);
    canvas.drawString(row ? "This dial is player" : "Room", 120, y - 25);
    if (sel) canvas.drawRoundRect(72, y - 16, 96, 33, 12, col);
    char v[12];
    if (row) {
      if (settings.mySeat) snprintf(v, sizeof(v), "P%d", settings.mySeat);
      else snprintf(v, sizeof(v), "None");
    } else {
      if (connRoom) snprintf(v, sizeof(v), "%d", connRoom);
      else snprintf(v, sizeof(v), "Off");
    }
    canvas.setFont(&fonts::DejaVu24);
    canvas.setTextColor(row ? (settings.mySeat ? col : th.muted) : (on ? th.text : th.muted));
    canvas.drawString(v, 120, y + 1);
    for (int k = 0; k < 2; k++) {
      int x = k ? 190 : 50;
      canvas.fillCircle(x, y, CONN_BTN_R, th.surface2);
      canvas.fillRoundRect(x - 8, y - 2, 16, 4, 2, sel ? col : th.text);
      if (k) canvas.fillRoundRect(x - 2, y - 8, 4, 16, 2, sel ? col : th.text);
    }
  }

  // Seats at this table: filled where a dial sits (this one ringed).
  int n = game.numPlayers;
  int gap = 22;
  for (int i = 0; i < n; i++) {
    int x = 120 + (int)((i - (n - 1) / 2.f) * gap), y = 156;
    uint32_t c = playerColor(i);
    bool mine = settings.mySeat == i + 1;
    bool dial = mine || (settings.linkRoom && net::seatHasDial(i));
    if (dial) canvas.fillCircle(x, y, 7, c);
    else canvas.drawCircle(x, y, 7, mix(c, th.bg, 0.3f));
    if (mine) canvas.drawCircle(x, y, 9, th.text);
  }
  char st[32];
  if (connRoom != settings.linkRoom) snprintf(st, sizeof(st), connRoom ? "joins room %d on Done" : "unlinks on Done", connRoom);
  else if (!settings.linkRoom) snprintf(st, sizeof(st), "not linked");
  else if (net::peerCount()) snprintf(st, sizeof(st), "%d other dial%s linked", net::peerCount(), net::peerCount() == 1 ? "" : "s");
  else snprintf(st, sizeof(st), "waiting for other dials");
  canvas.setFont(&fonts::DejaVu9);
  canvas.setTextColor(connRoom != settings.linkRoom ? th.gold : th.muted);
  canvas.drawString(st, 120, 174);

  // Phone app | Done
  bool ph = web::enabled();
  uint32_t pf = ph ? th.good : th.surface2;
  canvas.fillRoundRect(50, CONN_DONE_Y - 13, 66, 26, 13, pf);
  canvas.setTextColor(ph ? textOn(th.good) : th.text);
  canvas.setFont(&fonts::DejaVu12);
  canvas.drawString("Phone", 83, CONN_DONE_Y + 1);
  canvas.fillRoundRect(124, CONN_DONE_Y - 13, 66, 26, 13, th.accent);
  canvas.setTextColor(textOn(th.accent));
  canvas.drawString("Done", 157, CONN_DONE_Y + 1);
}

// ---------------------------------------------------------------- phone app
// Turns the companion app's Wi-Fi on or off and shows a QR code to join it.
void handlePhone(const InputEvents &ev) {
  bool on = web::enabled();
  if (ev.click) {
    if (on) openConnect();
    else web::setEnabled(true);
  }
  if (ev.longPress) {
    openConnect();
    return;
  }
  if (!ev.tap) return;
  if (!on) {
    if (abs(ev.y - 156) <= 22 && abs(ev.x - 120) <= 70) web::setEnabled(true);
    else if (ev.y > 186 || ev.y < 40) openConnect();
    return;
  }
  if (ev.y >= 32 && ev.y <= 152 && abs(ev.x - 120) <= 60) phoneQrUrl = !phoneQrUrl;
  else if (abs(ev.y - 214) <= 14 && ev.x < 120 && ev.x > 60) web::setEnabled(false);
  else if (abs(ev.y - 214) <= 14 && ev.x >= 120 && ev.x < 180) openConnect();
  else if (ev.y < 30) openConnect();
}

void drawPhone() {
  const Theme &th = T();
  canvas.fillScreen(th.bg);
  canvas.setTextDatum(textdatum_t::middle_center);
  if (!web::enabled()) {
    canvas.setFont(&fonts::DejaVu18);
    canvas.setTextColor(th.text);
    canvas.drawString("Phone app", 120, 40);
    canvas.setFont(&fonts::DejaVu12);
    canvas.setTextColor(th.muted);
    canvas.drawString("Manage the game on a", 120, 68);
    canvas.drawString("bigger screen. This dial", 120, 84);
    canvas.drawString("makes a Wi-Fi network", 120, 100);
    canvas.drawString("for your phone.", 120, 116);
    canvas.fillRoundRect(52, 156 - 18, 136, 36, 18, th.good);
    canvas.setTextColor(textOn(th.good));
    canvas.setFont(&fonts::DejaVu18);
    canvas.drawString("Turn on", 120, 157);
    drawIcon(canvas, IC_BACK, 120, 206, 14, th.muted, th.bg);
    return;
  }
  canvas.setFont(&fonts::DejaVu12);
  canvas.setTextColor(th.good);
  canvas.drawString(phoneQrUrl ? "Open the app" : "Join the Wi-Fi", 120, 20);
  char qr[96];
  if (phoneQrUrl) snprintf(qr, sizeof(qr), "http://%s/", web::address());
  else snprintf(qr, sizeof(qr), "WIFI:T:WPA;S:%s;P:%s;;", web::ssid(), web::password());
  canvas.fillRoundRect(60, 32, 120, 120, 8, 0xFFFFFF);
  canvas.qrcode(qr, 120 - 43, 92 - 43, 87, 3);  // version 3 = 29 modules of 3 px, centred in the white card
  canvas.setTextColor(th.text);
  canvas.setFont(&fonts::DejaVu12);
  canvas.drawString(web::ssid(), 120, 166);
  char line[48];
  snprintf(line, sizeof(line), "password %s", web::password());
  canvas.setFont(&fonts::DejaVu9);
  canvas.setTextColor(th.muted);
  canvas.drawString(line, 120, 180);
  int n = web::phones();
  if (n) snprintf(line, sizeof(line), "%d phone%s on - open %s", n, n == 1 ? "" : "s", web::address());
  else snprintf(line, sizeof(line), "tap the code for the app link");
  canvas.setTextColor(n ? th.good : th.muted);
  canvas.drawString(line, 120, 193);
  canvas.fillRoundRect(64, 202, 52, 24, 12, th.surface2);
  canvas.setTextColor(th.bad);
  canvas.setFont(&fonts::DejaVu12);
  canvas.drawString("Off", 90, 215);
  canvas.fillRoundRect(124, 202, 52, 24, 12, th.accent);
  canvas.setTextColor(textOn(th.accent));
  canvas.drawString("Done", 150, 215);
}

void render(uint32_t now) {
  if (logoOn(now)) {
    canvas.pushImage(0, 0, LOGO_W, LOGO_H, LOGO_565);
    canvas.pushSprite(tft, 0, 0);
    return;
  }
  if (screen == SCR_PLAYER || screen == SCR_MENU) buildRows();
  switch (screen) {
    case SCR_TABLE: drawTable(now, false); break;
    case SCR_SPIN: drawSpin(now); break;
    case SCR_VICTORY: drawVictory(now); break;
    case SCR_KO: drawKnockout(now); break;
    case SCR_COLOR: drawColorPicker(); break;
    case SCR_NAME: drawNameEditor(); break;
    case SCR_PLAYER: drawTiles(); break;
    case SCR_MENU: drawList("Game", T().accent); break;
    case SCR_POPUP: drawPopup(); break;
    case SCR_EDIT: drawEditor(now); break;
    case SCR_ATTACK: drawAttack(); break;
    case SCR_SPLASH: drawSplash(now); break;
    case SCR_DICE: drawDice(now); break;
    case SCR_CONNECT: drawConnect(); break;
    case SCR_PHONE: drawPhone(); break;
  }
  if (holdShownMs) drawHoldOverlay();
  canvas.pushSprite(tft, 0, 0);
}

// ---------------------------------------------------------------- knockout celebrations
uint8_t outMask() {
  uint8_t m = 0;
  for (int i = 0; i < game.numPlayers; i++)
    if (mtg::isOut(i)) m |= 1 << i;
  return m;
}

uint32_t koColor() {
  switch (koReason) {
    case OUT_POISON: return counterColor(C_POISON);
    case OUT_COMMANDER: return koSource >= 0 ? playerColor(koSource) : T().bad;
    case OUT_LIFE: return T().bad;
    default: return T().text;
  }
}

void startKnockout(int player, uint32_t now) {
  koPlayer = player;
  koReason = mtg::outReason(player);
  koSource = -1;
  int best = 0;
  for (int j = 0; j < game.numPlayers; j++)
    for (int k = 0; k < 2; k++)
      if (game.p[player].cmd[j][k] > best) best = game.p[player].cmd[j][k], koSource = j;
  uint32_t c = koColor();
  for (int i = 0; i < KO_PARTS; i++) {
    Confetti &p = koParts[i];
    p.color = c;
    p.phase = frand(0, 6.28f);
    if (koReason == OUT_POISON) {  // bubbles rising from the bottom
      p.x = frand(40, 200);
      p.y = frand(200, 330);
      p.vx = 0;
      p.vy = -frand(40, 100);
      p.w = 2 + esp_random() % 5;
    } else if (koReason == OUT_LIFE) {  // drops bursting out of the heart
      float a = frand(-160, -20) * DEG_TO_RAD, sp = frand(60, 170);
      p.x = 120;
      p.y = 96;
      p.vx = cosf(a) * sp;
      p.vy = sinf(a) * sp;
      p.w = 3 + esp_random() % 3;
    } else if (koReason == OUT_COMMANDER) {  // shards flying from the broken shield
      float a = frand(0, 360) * DEG_TO_RAD, sp = frand(70, 210);
      p.x = 120;
      p.y = 92;
      p.vx = cosf(a) * sp;
      p.vy = sinf(a) * sp;
      p.w = 3 + esp_random() % 4;
      if (i % 3 == 0) p.color = T().text;
    } else {
      p.y = 999;  // concede: no particles
    }
    p.h = p.w;
  }
  koReturn = screen;
  koStart = koLastFrame = now;
  screen = SCR_KO;
  dragActive = false;
  requestRedraw();
}

void finishKnockout() {
  uint8_t pending = koQueue & outMask();
  koQueue = 0;
  char tmp[24];
  if (pending && !mtg::gameResult(tmp, sizeof(tmp))) {  // several players fell at once: next one
    int next = __builtin_ctz(pending);
    koQueue = pending & ~(1 << next);
    Screen ret = koReturn;
    startKnockout(next, millis());
    koReturn = ret;
    return;
  }
  screen = koReturn;
  if (screen == SCR_KO || screen == SCR_SPIN || screen == SCR_VICTORY || screen == SCR_POPUP) screen = SCR_TABLE;
  if (screen == SCR_PLAYER || screen == SCR_MENU) buildRows();
  requestRedraw();
}

void checkKnockouts(uint32_t now) {
  uint8_t m = outMask();
  bool sameGame = game.gameId == koGameId;
  uint8_t fresh = sameGame ? (m & ~prevOutMask) : 0;
  prevOutMask = m;
  koGameId = game.gameId;
  if (screen == SCR_KO && !(m & (1 << koPlayer))) {  // knockout was undone
    koQueue = 0;
    finishKnockout();
    return;
  }
  char tmp[24];
  if (!fresh || mtg::gameResult(tmp, sizeof(tmp))) return;  // the victory party takes over
  if (screen == SCR_SPIN || screen == SCR_VICTORY || screen == SCR_SPLASH || screen == SCR_DICE) return;
  if (screen == SCR_KO) {
    koQueue |= fresh;
    return;
  }
  int first = __builtin_ctz(fresh);
  koQueue = fresh & ~(1 << first);
  startKnockout(first, now);
}

// Draws an icon cut down the middle, the halves pushed apart by `d` and dropped by `drop`.
void drawSplitIcon(Icon ic, int cx, int cy, int size, uint32_t col, float d, float drop) {
  const Theme &th = T();
  // Each half is clipped at its own (moved) centre line, so the gap between them shows.
  int l = cx - (int)d, r = cx + (int)d;
  canvas.setClipRect(0, 0, l, SCREEN_H);
  drawIcon(canvas, ic, l, cy + (int)drop, size, col, th.bg);
  canvas.setClipRect(r, 0, SCREEN_W - r, SCREEN_H);
  drawIcon(canvas, ic, r, cy + (int)(drop * 0.6f), size, col, th.bg);
  canvas.clearClipRect();
}

void drawKnockout(uint32_t now) {
  const Theme &th = T();
  uint32_t t = now - koStart;
  float ts = t / 1000.f;
  float dt = min(0.05f, (now - koLastFrame) / 1000.f);
  koLastFrame = now;
  uint32_t c = koColor();
  canvas.fillScreen(th.bg);

  // Pulsing rim and a soft glow behind the icon.
  float pulse = 0.5f + 0.5f * sinf(ts * 9);
  canvas.fillArc(120, 120, 110, 119, 0, 360, mix(c, th.bg, 0.25f + 0.45f * (1 - pulse)));
  canvas.fillCircle(120, 94, 62, mix(c, th.bg, 0.88f));

  // Pop-in scale with a little overshoot.
  float s = min(1.f, t / 380.f);
  float pop = s < 1 ? 1 + 0.25f * sinf(s * 3.14159f) : 1;
  int size = (int)(96 * s * pop);

  if (koReason == OUT_POISON) {
    for (auto &p : koParts) {  // bubbles behind the skull
      p.y += p.vy * dt;
      p.x += sinf(ts * 4 + p.phase) * 0.5f;
      if (p.y < 20) p.y = 240 + frand(0, 40);
      canvas.drawCircle((int)p.x, (int)p.y, p.w, mix(c, th.bg, 0.3f));
      canvas.drawCircle((int)p.x, (int)p.y, p.w + 1, mix(c, th.bg, 0.6f));
    }
    int wob = t < 900 ? (int)(sinf(ts * 30) * 4) : 0;
    if (size > 4) drawIcon(canvas, IC_POISON, 120 + wob, 94, size, c, th.bg);
  } else if (koReason == OUT_COMMANDER || koReason == OUT_LIFE) {
    Icon ic = koReason == OUT_LIFE ? IC_LIFE : IC_COMMANDER;
    const uint32_t breakAt = 650;
    if (t < breakAt) {
      if (size > 4) drawIcon(canvas, ic, 120, 94, size, c, th.bg);
      if (koReason == OUT_COMMANDER && t > 420) {  // the slash
        float k = min(1.f, (t - 420) / 180.f);
        canvas.drawWideLine(150, 30, (int)(150 - 60 * k), (int)(30 + 130 * k), 3.5f, th.text);
      }
    } else {
      float k = (t - breakAt) / 1000.f;
      drawSplitIcon(ic, 120, 94, 96, c, 6 + 34 * min(1.f, k * 1.5f), 40 * k * k);
      for (auto &p : koParts) {  // shards / drops
        p.vy += 220 * dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        if (p.y > 245) continue;
        if (koReason == OUT_LIFE) {
          canvas.fillCircle((int)p.x, (int)p.y, p.w / 2 + 1, p.color);
        } else {
          float a = ts * 8 + p.phase;
          canvas.fillTriangle((int)(p.x + cosf(a) * p.w), (int)(p.y + sinf(a) * p.w),
                              (int)(p.x + cosf(a + 2.2f) * p.w), (int)(p.y + sinf(a + 2.2f) * p.w),
                              (int)(p.x + cosf(a + 4.1f) * p.w), (int)(p.y + sinf(a + 4.1f) * p.w), p.color);
        }
      }
    }
  } else {  // concede: a waving white flag
    int wave = (int)(sinf(ts * 6) * 5);
    if (size > 4) drawIcon(canvas, IC_FLAG, 120 + wave, 94, size, th.text, th.bg);
  }

  // Who, and how.
  char name[24], how[40];
  mtg::playerName(koPlayer, name, sizeof(name));
  switch (koReason) {
    case OUT_POISON: snprintf(how, sizeof(how), "Poisoned"); break;
    case OUT_LIFE: snprintf(how, sizeof(how), "Out of life"); break;
    case OUT_CONCEDE: snprintf(how, sizeof(how), "Conceded"); break;
    case OUT_COMMANDER: {
      char src[12] = "?";
      if (koSource >= 0) mtg::shortName(koSource, src, sizeof(src));
      snprintf(how, sizeof(how), "Commander damage from %s", src);
      break;
    }
    default: snprintf(how, sizeof(how), "Knocked out"); break;
  }
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setFont(&fonts::DejaVu24);
  if (canvas.textWidth(name) > 190) canvas.setFont(&fonts::DejaVu18);
  outlinedText(name, 120, 170, playerColor(koPlayer), th.bg);
  canvas.setFont(&fonts::DejaVu12);
  if (canvas.textWidth(how) > 170) canvas.setFont(&fonts::DejaVu9);
  outlinedText(how, 120, 196, koReason == OUT_CONCEDE ? th.muted : c, th.bg);
}

// Winner colour and title lines for the celebration.
uint32_t winnerColor(uint8_t mask) {
  if (!mask) return T().muted;
  if (game.format == FMT_EMPEROR || game.format == FMT_RIFT_2V2) return TEAM_COLORS[mtg::team(__builtin_ctz(mask))];
  if (!(mask & (mask - 1))) return playerColor(__builtin_ctz(mask));
  return T().gold;  // several winners (heroes)
}

void winnerLines(uint8_t mask, char *l1, char *l2, int len) {
  if (!mask) {
    snprintf(l1, len, "Draw");
    snprintf(l2, len, "no winner");
  } else if (game.format == FMT_EMPEROR || game.format == FMT_RIFT_2V2) {
    snprintf(l1, len, "Team %c", 'A' + mtg::team(__builtin_ctz(mask)));
    snprintf(l2, len, "wins!");
  } else if (game.format == FMT_ARCHENEMY && mask != 0x01) {
    snprintf(l1, len, "Heroes");
    snprintf(l2, len, "win!");
  } else {
    mtg::playerName(__builtin_ctz(mask), l1, len);
    snprintf(l2, len, "wins!");
  }
}

float frand(float lo, float hi) { return lo + (hi - lo) * (esp_random() % 10000) / 10000.f; }

void startVictory(uint32_t now) {
  uint8_t mask = mtg::winnerMask();
  uint32_t wc = winnerColor(mask);
  const uint32_t palette[] = {wc, T().gold, 0xFFFFFF, wc, playerColor(esp_random() % MAX_PLAYERS)};
  for (int i = 0; i < CONFETTI; i++) {  // burst from just above the centre
    Confetti &c = confetti[i];
    float a = frand(-165, -15) * DEG_TO_RAD, sp = frand(60, 260);
    c.x = 120 + frand(-8, 8);
    c.y = 100 + frand(-8, 8);
    c.vx = cosf(a) * sp;
    c.vy = sinf(a) * sp;
    c.color = palette[i % 5];
    if (i % 7 == 6) c.color = playerColor(esp_random() % MAX_PLAYERS);
    c.w = 5 + esp_random() % 5;
    c.h = 3 + esp_random() % 4;
    c.phase = frand(0, 6.28f);
  }
  victoryStart = victoryLastFrame = now;
  dragActive = false;
  editing = false;
  screen = SCR_VICTORY;
  requestRedraw();
}

void checkVictory(uint32_t now) {
  char tmp[24];
  bool over = mtg::gameResult(tmp, sizeof(tmp));
  if (over && !wasOver && screen != SCR_SPIN && screen != SCR_SPLASH && screen != SCR_DICE) startVictory(now);
  if (!over && screen == SCR_VICTORY) openTable();  // winning hit was undone
  wasOver = over;
}

// Text with a 2px outline so it stays readable over rays and confetti.
void outlinedText(const char *s, int x, int y, uint32_t fg, uint32_t outline) {
  canvas.setTextColor(outline);
  for (int dx = -2; dx <= 2; dx++)
    for (int dy = -2; dy <= 2; dy++)
      if (dx || dy) canvas.drawString(s, x + dx, y + dy);
  canvas.setTextColor(fg);
  canvas.drawString(s, x, y);
}

void drawVictory(uint32_t now) {
  const Theme &th = T();
  uint32_t t = now - victoryStart;
  bool animating = t < VICTORY_ANIM_MS;
  float ts = (animating ? t : VICTORY_ANIM_MS) / 1000.f;
  float dt = min(0.05f, (now - victoryLastFrame) / 1000.f);
  victoryLastFrame = now;
  uint8_t mask = mtg::winnerMask();
  uint32_t wc = winnerColor(mask);

  canvas.fillScreen(th.bg);
  // Slowly turning light rays.
  uint32_t ray = mix(wc, th.bg, settings.theme ? 0.82f : 0.8f);
  for (int k = 0; k < 12; k++) {
    float a0 = (ts * 12 + k * 30) * DEG_TO_RAD, a1 = a0 + 14 * DEG_TO_RAD;
    canvas.fillTriangle(120, 100, 120 + cosf(a0) * 200, 100 + sinf(a0) * 200, 120 + cosf(a1) * 200,
                        100 + sinf(a1) * 200, ray);
  }
  // Pulsing rim.
  float pulse = 0.5f + 0.5f * sinf(ts * 6);
  canvas.fillArc(120, 120, 112, 119, 0, 360, mix(wc, th.bg, 0.6f - 0.5f * pulse));

  // Confetti: physics, then fluttering rectangles.
  for (int i = 0; i < CONFETTI; i++) {
    Confetti &c = confetti[i];
    if (animating) {
      c.vy += 170 * dt;
      c.vx *= 0.995f;
      c.x += c.vx * dt + sinf(ts * 3 + c.phase) * 0.6f;
      c.y += c.vy * dt;
      if (c.y > 250 && t < VICTORY_ANIM_MS - 2500) {  // keep it raining for a while
        c.x = frand(20, 220);
        c.y = frand(-30, -5);
        c.vx = frand(-20, 20);
        c.vy = frand(20, 70);
      }
    }
    if (c.y < -10 || c.y > 245) continue;
    int w = max(1, (int)(c.w * fabsf(sinf(ts * 9 + c.phase))));  // spinning paper
    canvas.fillRect((int)c.x - w / 2, (int)c.y, w, c.h, c.color);
  }

  // Crown (or flag for a draw), bobbing.
  int bob = (int)(sinf(ts * 3) * 4);
  Icon crown = mask ? IC_MONARCH : IC_FLAG;
  drawIcon(canvas, crown, 120, 70 + bob, 70, th.bg, th.bg);  // outline so rays don't touch it
  drawIcon(canvas, crown, 120, 70 + bob, 58, mask ? th.gold : th.muted, th.bg);

  char l1[24], l2[24];
  winnerLines(mask, l1, l2, sizeof(l2));
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setFont(&fonts::DejaVu24);
  if (canvas.textWidth(l1) > 176) canvas.setFont(&fonts::DejaVu18);
  outlinedText(l1, 120, 130, wc, th.bg);
  canvas.setFont(&fonts::DejaVu18);
  outlinedText(l2, 120, 155, th.gold, th.bg);

  // "Play again?" button, shown once the celebration has had a moment.
  if (t >= PLAY_AGAIN_DELAY_MS) {
    uint32_t btn = mask ? wc : th.accent;
    canvas.fillRoundRect(44, PLAY_BTN_Y - 17, 152, 34, 17, th.bg);
    canvas.fillRoundRect(46, PLAY_BTN_Y - 15, 148, 30, 15, btn);
    drawIcon(canvas, IC_REFRESH, 66, PLAY_BTN_Y, 16, textOn(btn), btn);
    canvas.setTextColor(textOn(btn));
    canvas.setFont(&fonts::DejaVu18);
    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.drawString("Play again?", 80, PLAY_BTN_Y + 1);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setFont(&fonts::DejaVu9);
    outlinedText("press to play again", 120, PLAY_BTN_Y + 28, th.text, th.bg);
  }
}

// Same format, players, life and partners as the game that just ended; spinner on every dial.
void playAgain() {
  settings.format = game.format;
  settings.numPlayers = game.numPlayers;
  settings.startLife = game.startLife;
  settings.partners = game.partners;
  mtg::markSettingsDirty();
  mtg::newGame();
  wasOver = false;
  startSpinner();
}

// Re-open the result (without replaying the animation) from the finished table.
void showPlayAgainPrompt() {
  victoryStart = millis() - VICTORY_ANIM_MS;
  victoryLastFrame = millis();
  screen = SCR_VICTORY;
  requestRedraw();
}

void updateLeds() {
  if (logoOn(millis()) && settings.leds && !asleep) {  // the logo's colours sweep around the ring
    static const uint32_t LOGO_LED[5] = {0x0060FF, 0x00C040, 0x8000FF, 0xFF2000, 0xFF8000};
    static uint32_t lastShow = 0;
    uint32_t now = millis();
    if (now - lastShow < 30) return;
    lastShow = now;
    float in = min(1.f, (LOGO_MS - (logoUntil - now)) / 400.f);
    for (int i = 0; i < NUM_LEDS; i++) {
      uint32_t c = mix(0, LOGO_LED[(i + now / 160) % 5], 0.35f * in);
      leds.setPixelColor(i, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    }
    leds.show();
    lastLedColor = 0xFFFFFFFF;
    return;
  }
  if (screen == SCR_DICE && settings.leds && !asleep) {
    static uint32_t lastShow = 0;
    uint32_t now = millis();
    if (now - lastShow < 30) return;
    lastShow = now;
    uint32_t dc = DIE_COLORS[dieSel];
    bool crit = !dieRolling && DIE_SIDES[dieSel] == 20 && dieResult == 20;
    for (int i = 0; i < NUM_LEDS; i++) {
      uint32_t c;
      if (dieRolling) c = mix(0, dc, ((now / 70 + i) % NUM_LEDS) == 0 ? 0.6f : 0.06f);  // spinning dot
      else if (crit) c = mix(0, 0xF5C542, 0.25f + 0.25f * sinf(now / 120.f));
      else c = mix(0, dc, now - rollStart < ROLL_MS + 400 && dieResult ? 0.5f : 0.15f);
      leds.setPixelColor(i, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    }
    leds.show();
    lastLedColor = 0xFFFFFFFF;
    return;
  }
  if (screen == SCR_SPLASH && settings.leds && !asleep) {  // mana colours, or the chosen format's
    static uint32_t lastShow = 0;
    uint32_t now = millis();
    if (now - lastShow < 30) return;
    lastShow = now;
    float t = now - splashStart;
    for (int i = 0; i < NUM_LEDS; i++) {
      uint32_t c;
      if (splashStep >= 1) {
        c = mix(0, FORMAT_COLORS[splashFormat], 0.2f + 0.06f * sinf(now / 400.f));
      } else {
        float on = constrain((t - i * 120) / 300.f, 0.f, 1.f);
        c = mix(0, MANA_LED[i % 5], on * (0.16f + 0.07f * sinf(now / 600.f + i * 1.3f)));
      }
      leds.setPixelColor(i, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    }
    leds.show();
    lastLedColor = 0xFFFFFFFF;
    return;
  }
  if (screen == SCR_KO && settings.leds && !asleep) {  // pulse in the knockout's colour
    static uint32_t lastShow = 0;
    uint32_t now = millis();
    if (now - lastShow < 30) return;
    lastShow = now;
    float pulse = 0.5f + 0.5f * sinf((now - koStart) / 1000.f * 9);
    uint32_t c = mix(0, koColor(), 0.08f + 0.5f * pulse);
    for (int i = 0; i < NUM_LEDS; i++) leds.setPixelColor(i, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    leds.show();
    lastLedColor = 0xFFFFFFFF;
    return;
  }
  if (screen == SCR_VICTORY && settings.leds && !asleep) {  // chase + gold flashes
    static uint32_t lastShow = 0;
    uint32_t now = millis();
    if (now - lastShow < 30) return;
    lastShow = now;
    uint32_t t = now - victoryStart;
    uint32_t wc = winnerColor(mtg::winnerMask());
    bool flash = (t % 1500) < 140;
    float head = (t / 90.f);
    for (int i = 0; i < NUM_LEDS; i++) {
      uint32_t c;
      if (flash) {
        c = 0xC08A10;
      } else {
        float d = fmodf(head - i + NUM_LEDS * 100, NUM_LEDS);  // distance behind the head
        c = mix(0, wc, max(0.04f, 0.75f - d * 0.22f));
      }
      if (t > VICTORY_ANIM_MS) c = mix(0, 0xF5C542, 0.25f);  // settle to a warm gold glow
      leds.setPixelColor(i, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    }
    leds.show();
    lastLedColor = 0xFFFFFFFF;
    return;
  }
  uint32_t c = 0;
  if (settings.leds && !asleep) {
    char tmp[24];
    if (mtg::gameResult(tmp, sizeof(tmp))) c = 0x302000;
    else if (screen == SCR_COLOR) c = mix(0, PALETTE[colorSel], 0.18f);  // live preview
    else c = mix(0, mtg::isOut(focus) ? 0x404040 : playerColor(focus), 0.18f);
  }
  if (c == lastLedColor) return;
  lastLedColor = c;
  for (int i = 0; i < NUM_LEDS; i++) leds.setPixelColor(i, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
  leds.show();
}

// ---------------------------------------------------------------- input per screen
int hitSeat(int x, int y, float *radius) {
  float dx = x - 120, dy = y - 120;
  *radius = sqrtf(dx * dx + dy * dy);
  float ang = atan2f(dy, dx) * RAD_TO_DEG;
  float rel = fmodf(ang - 90.f + 720.f, 360.f);
  float seg = 360.f / game.numPlayers;
  return ((int)floorf(rel / seg + 0.5f)) % game.numPlayers;
}

void handleTable(const InputEvents &ev) {
  if (ev.detents) {
    int before = game.p[focus].life;
    mtg::addLife(focus, ev.detents * mtg::lifeStep());
    if (game.p[focus].life != before) noteLifeChange(focus, game.p[focus].life - before);
  }
  if (ev.click) {
    char tmp[24];
    if (mtg::gameResult(tmp, sizeof(tmp))) {
      showPlayAgainPrompt();  // game's over: the dial button offers a rematch
    } else {
      focus = (focus + 1) % game.numPlayers;
      mtg::markUndo();
    }
  }
  if (ev.doubleClick) toast(mtg::undo() ? "Undone" : "Nothing to undo");
  if (ev.longPress) openPlayer();
  if (ev.tap) {
    float r;
    int seat = hitSeat(ev.x, ev.y, &r);
    if (r >= 80) {
      focus = seat;
      mtg::markUndo();
    } else {
      openPlayer();
    }
  }
  if (ev.dragging) {
    float r;
    int seat = hitSeat(ev.sx, ev.sy, &r);
    if (r >= 66 && !mtg::isOut(seat) && attacksAllowed()) {
      dragActive = true;
      dragFrom = seat;
      dragX = ev.x;
      dragY = ev.y;
    }
  }
  if (ev.dragEnd) {
    if (dragActive) {
      int to = dragTarget(ev.x, ev.y);
      if (to >= 0) openAttack(dragFrom, to);
    }
    dragActive = false;
  }
}

void handleAttack(const InputEvents &ev) {
  if (ev.detents) attackAmount = constrain(attackAmount + ev.detents * mtg::lifeStep(), 0, 99 * mtg::lifeStep());
  if (ev.click) applyAttack();
  if (ev.longPress) openTable();
  if (ev.tap) {
    int chip = attackChipAt(ev.x, ev.y);
    int step = stepButtonAt(ev.x, ev.y);
    if (step) {
      attackAmount = constrain(attackAmount + step * mtg::lifeStep(), 0, 99 * mtg::lifeStep());
    } else if (chip >= 0) {
      tapAttackChip(chip);
    } else if (ev.y < 52) {
      openTable();  // tap the title to cancel
    } else {
      applyAttack();
    }
  }
}

void handleList(const InputEvents &ev) {
  int count = rows.size();
  if (ev.detents) {
    if (editing) adjustRow(rows[listSel], ev.detents);
    else listSel = constrain(listSel + ev.detents, 0, count - 1);
  }
  if (ev.click) pressRow(rows[listSel]);
  if (ev.longPress) {
    if (editing) {
      editing = false;
      finishEdit(rows[listSel]);
    }
    else openTable();
  }
  if (ev.tap) {
    if (ev.y < 40) {
      openTable();
    } else {
      int k = (int)floorf((ev.y - 126 + 18) / 36.f);
      int idx = listSel + k;
      if (idx >= 0 && idx < count) {
        if (k == 0) {
          pressRow(rows[idx]);
        } else {
          if (editing) finishEdit(rows[listSel]);
          editing = false;
          listSel = idx;
        }
      }
    }
  }
}

}  // namespace

namespace ui {

LGFX_Sprite &frame() { return canvas; }

void showGallery() {
  const Theme &th = T();
  canvas.fillScreen(th.bg);
  int n = IC_COUNT - 1;
  for (int i = 0; i < n; i++) {
    int col = i % 6, row = i / 6;
    int x = 40 + col * 32, y = 40 + row * 32;
    drawIcon(canvas, (Icon)(i + 1), x, y, 24, th.text, th.bg);
  }
  canvas.pushSprite(tft, 0, 0);
}

void begin(LGFX &d) {
  tft = &d;
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_BACKLIGHT, 0);
  setBacklight(settings.brightness);

  canvas.setColorDepth(16);
  canvas.setPsram(false);
  if (!canvas.createSprite(SCREEN_W, SCREEN_H)) {
    canvas.setPsram(true);
    canvas.createSprite(SCREEN_W, SCREEN_H);
  }
  center.setColorDepth(16);
  center.setPsram(true);
  if (!center.createSprite(156, 156)) {
    center.setPsram(false);
    center.createSprite(156, 156);
  }
  label.setColorDepth(16);
  label.setPsram(false);
  label.createSprite(64, 28);
  Serial.printf("psram=%u heap=%u canvas=%p center=%p label=%p\n", (unsigned)ESP.getPsramSize(),
                (unsigned)ESP.getFreeHeap(), canvas.getBuffer(), center.getBuffer(), label.getBuffer());

  leds.begin();
  leds.clear();
  leds.show();

  input::setReversed(settings.reverseDial);
  if (settings.mySeat && settings.mySeat <= game.numPlayers) focus = settings.mySeat - 1;
  if (focus >= game.numPlayers) focus = 0;
  seenGameId = game.gameId;
  seenFirst = game.firstPlayer;
  lastFocusLife = game.p[focus].life;
  {
    char tmp[24];
    wasOver = mtg::gameResult(tmp, sizeof(tmp));  // no celebration for a game already won at boot
    prevOutMask = outMask();
    koGameId = game.gameId;
  }
  lastInputAt = millis();
  openSplash(true);
  logoGameId = game.gameId;
  showLogo();                 // power-on: logo first, then the picker's intro
  splashStart = logoUntil;
}

void handle(const InputEvents &ev, uint32_t now) {
  // Drop the overlay when the finger lifts or moves. Touch is polled every 15 ms, so allow
  // a short gap between hold reports before deciding the hold has ended.
  if (holdShownMs && !ev.holding && !ev.btnHoldMs &&
      (ev.holdRelease || ev.dragging || ev.dragEnd || ev.tap || now - lastHoldEventAt > 150)) {
    holdShownMs = 0;
    requestRedraw();
  }
  if (!ev.any()) {
    if (input::active()) lastInputAt = now;
    return;
  }
  lastInputAt = now;
  if (asleep) {  // first input only wakes the screen
    asleep = false;
    requestRedraw();
    return;
  }
  // Three quick presses of the screen: back to the game selector (the game is kept for Resume).
  // Not in the name editor, where quick presses skip through the letters.
  if (ev.tripleClick && screen != SCR_NAME) {
    goHome();
    return;
  }
  if (logoOn(now)) {
    if (ev.click || ev.tap || ev.doubleClick || ev.tripleClick) {
      logoUntil = now;
      if (screen == SCR_SPIN && !spinDone) spinStart = now;
      if (screen == SCR_SPLASH) splashStart = now;
      requestRedraw();
    }
    return;
  }
  if (screen == SCR_SPLASH) {
    if (ev.doubleClick) return;  // two quick presses: ignore rather than skip two steps
    handleSplash(ev, now);
    requestRedraw();
    return;
  }
  if (screen == SCR_PHONE && !ev.btnHoldMs && !ev.holdReset) {
    if (!ev.holding && !ev.holdRelease) handlePhone(ev);
    requestRedraw();
    return;
  }
  if (screen == SCR_CONNECT && !ev.btnHoldMs && !ev.holdReset) {
    if (!ev.holding && !ev.holdRelease) handleConnect(ev);
    requestRedraw();
    return;
  }
  if (screen == SCR_DICE && !ev.btnHoldMs && !ev.holdReset) {
    if (!ev.holding && !ev.holdRelease) handleDice(ev, now);
    requestRedraw();
    return;
  }
  // Screen pressed in and held: reset countdown (the reset itself arrives as holdReset at 3 s).
  if (ev.btnHoldMs) {
    holdShownMs = ev.btnHoldMs;
    btnHolding = true;
    lastHoldEventAt = now;
    requestRedraw();
    return;
  }
  // Holding a -/+ button repeats (and speeds up); it never opens the menu or resets the game.
  if (stepHolding) {
    if (ev.holding) {
      if (now - stepLastAt >= 110) {
        stepCount++;
        applyStep(stepDir * (stepCount > 14 ? 5 : 1), now);
        stepLastAt = now;
      }
      return;
    }
    if (ev.holdRelease || ev.holdReset || ev.tap || ev.dragEnd) {
      stepHolding = false;
      return;
    }
  }
  if (ev.holding && stepButtonAt(ev.x, ev.y)) {
    stepHolding = true;
    stepDir = stepButtonAt(ev.x, ev.y);
    stepCount = 0;
    stepLastAt = now;
    applyStep(stepDir, now);
    return;
  }
  // A hold that opened the colour picker is finished once the finger lifts.
  if (holdConsumed) {
    if (ev.holdRelease || ev.holdReset) {
      holdConsumed = false;
      return;
    }
    if (ev.holding) return;
  }
  // Hold on a player's name: colour picker.
  if (ev.holding && holdOnName(ev.x, ev.y)) {
    holdConsumed = true;
    holdShownMs = 0;
    openColorPicker(focus);
    return;
  }
  // Finger held on the glass: menu on release (the reset is a press-in of the screen).
  if (ev.holding) {
    holdShownMs = max(ev.holdMs, HOLD_MS);
    btnHolding = false;
    lastHoldEventAt = now;
    requestRedraw();
    return;
  }
  if (ev.holdReset) {
    resetToDefault();
    return;
  }
  if (ev.holdRelease) {
    if (screen == SCR_TABLE || screen == SCR_PLAYER) openMenu();
    requestRedraw();
    return;
  }
  if (screen == SCR_NAME) {
    handleName(ev);
    requestRedraw();
    return;
  }
  if (screen == SCR_COLOR) {
    handleColor(ev);
    requestRedraw();
    return;
  }
  if (screen == SCR_KO) {
    if (ev.click || ev.tap || ev.longPress) finishKnockout();
    return;
  }
  if (screen == SCR_VICTORY) {
    bool ready = now - victoryStart >= PLAY_AGAIN_DELAY_MS;
    if (ev.doubleClick) {
      toast(mtg::undo() ? "Undone" : "Nothing to undo");  // undo the winning hit
    } else if (ev.click && ready) {
      playAgain();
    } else if (ev.tap && ready) {
      if (abs(ev.y - PLAY_BTN_Y) <= 22 && abs(ev.x - 120) <= 80) playAgain();
      else openTable();  // look at the final board
    } else if (ev.longPress) {
      openTable();
    }
    return;
  }
  if (screen == SCR_SPIN) {
    if (ev.click || ev.tap || ev.longPress) {
      if (spinDone) openTable();
      else spinStart = now - SPIN_MS;  // skip to the result
    }
    requestRedraw();
    return;
  }

  switch (screen) {
    case SCR_TABLE: handleTable(ev); break;
    case SCR_PLAYER: handleTiles(ev); break;
    case SCR_MENU: handleList(ev); break;
    case SCR_POPUP:
      if (now - popupAt > 250) {
        popupIcon = IC_NONE;
        openTable();
      }
      break;
    case SCR_ATTACK: handleAttack(ev); break;
    case SCR_EDIT:
      if (ev.detents) editStep(ev.detents, now);
      if (ev.tap && stepButtonAt(ev.x, ev.y)) {
        editStep(stepButtonAt(ev.x, ev.y), now);
        break;
      }
      if (ev.click || ev.longPress || ev.tap) {  // Done button, or tap anywhere else
        screen = editFrom;
        buildRows();
      }
      break;
  }
  if (focus >= game.numPlayers) focus = 0;
  requestRedraw();
}

void update(uint32_t now) {
  // Screens where two quick presses must not act before a possible third (undo, picker steps).
  input::setDoubleClickEnabled(screen == SCR_TABLE || screen == SCR_VICTORY || screen == SCR_SPLASH);

  bool shouldSleep = settings.sleepMinutes && now - lastInputAt > settings.sleepMinutes * 60000UL;
  if (shouldSleep && !asleep) asleep = true;
  setBacklight(asleep ? 0 : settings.brightness);
  updateLeds();

  if (confirmRow >= 0 && now - confirmAt >= 3000) {
    confirmRow = -1;
    requestRedraw();
  }
  syncFromLink(now);
  noteNewGame();
  bool logo = logoOn(now);
  checkKnockouts(now);
  checkVictory(now);
  if (screen == SCR_SPLASH && !asleep && !logo && now - splashStart < SPLASH_INTRO_MS + 50) requestRedraw();
  if (!asleep) updateDice(now);
  static uint32_t phoneTick = 0;  // phone screen: keep the connected-phone count fresh
  if ((screen == SCR_PHONE || screen == SCR_CONNECT) && !asleep && now - phoneTick > 1000) {
    phoneTick = now;
    requestRedraw();
  }
  if (screen == SCR_KO && !asleep) {
    if (now - koStart >= KO_MS) finishKnockout();
    else requestRedraw();
  }
  if (screen == SCR_VICTORY && !asleep) {
    uint32_t t = now - victoryStart;
    if (t < VICTORY_ANIM_MS + 100) requestRedraw();
  }
  if (screen == SCR_SPIN && !asleep && !logo) {
    if (!spinDone && now - spinStart >= SPIN_MS) {
      spinDone = true;
      spinDoneAt = now;
      requestRedraw();  // show the result
    }
    if (!spinDone) requestRedraw();  // animate
    else if (now - spinDoneAt >= SPIN_RESULT_MS) openTable();
  }
  if (timedRedrawAt && now >= timedRedrawAt) {
    timedRedrawAt = 0;
    requestRedraw();
  }
  if (dirty && !asleep) {
    dirty = false;
    render(now);
  }
  mtg::saveIfDirty(now);
}

void externalChange() {
  input::setReversed(settings.reverseDial);
  lastLedColor = 0xFFFFFFFF;
  if (focus >= game.numPlayers) focus = 0;
  if (screen == SCR_PLAYER || screen == SCR_MENU) buildRows();
  requestRedraw();
}

void startGame() {
  nameChain = 0;
  editing = false;
  dragActive = false;
  wasOver = false;
  focus = 0;
  startSpinner();
}

void whoGoesFirst() {
  if (screen == SCR_SPLASH || screen == SCR_DICE) return;
  startSpinner();
}

}  // namespace ui
