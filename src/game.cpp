#include "game.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

Game game;
Settings settings;

namespace {

constexpr uint32_t GAME_MAGIC = 0x4D544704;  // "MTG" v4
// Default palette picks: red, blue, green, amber, violet, teal.
const uint8_t DEFAULT_COLORS[MAX_PLAYERS] = {0, 11, 6, 3, 14, 8};
constexpr uint32_t SETTINGS_MAGIC = 0x4D545302;
constexpr int UNDO_SLOTS = 40;
constexpr uint32_t BURST_MS = 1500;  // edits closer than this share one undo step
constexpr uint32_t SAVE_DELAY_MS = 2000;

Game undoRing[UNDO_SLOTS];
int undoHead = 0;   // next write slot
int undoCount = 0;
uint32_t lastEditAt = 0;
bool forceUndoPoint = true;
uint32_t changes = 0;

bool gameDirty = false;
bool settingsDirty = false;
uint32_t dirtyAt = 0;

Preferences prefs;

template <typename T>
T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

void pushUndo() {
  undoRing[undoHead] = game;
  undoHead = (undoHead + 1) % UNDO_SLOTS;
  if (undoCount < UNDO_SLOTS) undoCount++;
}

void beginEdit() {
  uint32_t now = millis();
  changes++;
  if (forceUndoPoint || now - lastEditAt > BURST_MS) {
    pushUndo();
    forceUndoPoint = false;
  }
  lastEditAt = now;
  gameDirty = true;
  dirtyAt = now;
}

void defaultSettings() {
  memset(&settings, 0, sizeof(settings));
  settings.magic = SETTINGS_MAGIC;
  settings.theme = 0;
  settings.brightness = 80;
  settings.reverseDial = 0;
  settings.leds = 1;
  settings.facePlayer = 1;
  settings.cmdHitsLife = 1;
  settings.format = FMT_COMMANDER;
  settings.numPlayers = 4;
  settings.partners = 0;
  settings.startLife = 40;
  settings.sleepMinutes = 5;
  settings.linkRoom = 0;
  settings.mySeat = 0;
}

}  // namespace

namespace mtg {

const char *formatName(uint8_t f) {
  switch (f) {
    case FMT_STANDARD: return "Standard";
    case FMT_COMMANDER: return "Commander";
    case FMT_ARCHENEMY: return "Archenemy";
    case FMT_EMPEROR: return "Emperor";
    case FMT_YUGIOH: return "Yu-Gi-Oh!";
    case FMT_LORCANA: return "Lorcana";
    case FMT_POKEMON: return "Pokemon";
    case FMT_RIFTBOUND: return "Riftbound";
    case FMT_RIFT_2V2: return "Riftbound 2v2";
  }
  return "?";
}

uint8_t systemOf(uint8_t f) {
  switch (f) {
    case FMT_YUGIOH: return SYS_YUGIOH;
    case FMT_LORCANA: return SYS_LORCANA;
    case FMT_POKEMON: return SYS_POKEMON;
    case FMT_RIFTBOUND: case FMT_RIFT_2V2: return SYS_RIFTBOUND;
  }
  return SYS_MAGIC;
}

const char *systemName(uint8_t s) {
  static const char *const N[SYS_COUNT] = {"Magic", "Yu-Gi-Oh!", "Lorcana", "Pokemon", "Riftbound"};
  return s < SYS_COUNT ? N[s] : "?";
}

bool isMagic() { return systemOf(game.format) == SYS_MAGIC; }

LifeKind lifeKind(uint8_t f) {
  switch (systemOf(f)) {
    case SYS_LORCANA: case SYS_RIFTBOUND: return LK_RACE;
    case SYS_POKEMON: return LK_PRIZES;
  }
  return LK_LIFE;
}

LifeKind lifeKind() { return lifeKind(game.format); }

int lifeGoal() {
  switch (game.format) {
    case FMT_LORCANA: return 20;
    case FMT_RIFTBOUND: return 8;
    case FMT_RIFT_2V2: return 11;
  }
  return 0;
}

int lifeStep() { return game.format == FMT_YUGIOH ? 100 : 1; }

const char *lifeName() {
  switch (systemOf(game.format)) {
    case SYS_YUGIOH: return "LP";
    case SYS_LORCANA: return "Lore";
    case SYS_POKEMON: return "Prizes";
    case SYS_RIFTBOUND: return "Points";
  }
  return "Life";
}

int teamScore(int t) {
  int s = 0;
  for (int i = 0; i < game.numPlayers; i++)
    if (team(i) == t) s += game.p[i].life;
  return s;
}

int defaultLife(uint8_t f) {
  switch (f) {
    case FMT_COMMANDER: return 40;
    case FMT_YUGIOH: return 8000;
    case FMT_POKEMON: return 6;
    case FMT_LORCANA: case FMT_RIFTBOUND: case FMT_RIFT_2V2: return 0;
  }
  return 20;
}

int defaultPlayers(uint8_t f) {
  switch (f) {
    case FMT_STANDARD: return 2;
    case FMT_COMMANDER: return 4;
    case FMT_ARCHENEMY: return 4;
    case FMT_EMPEROR: return 6;
    case FMT_RIFT_2V2: return 4;
  }
  return 2;
}

int minPlayers(uint8_t f) {
  if (f == FMT_EMPEROR) return 6;
  if (f == FMT_ARCHENEMY) return 3;
  if (f == FMT_RIFT_2V2) return 4;
  if (systemOf(f) != SYS_MAGIC) return 2;
  return 1;
}

int maxPlayers(uint8_t f) {
  switch (f) {
    case FMT_POKEMON: return 2;
    case FMT_YUGIOH: case FMT_LORCANA: case FMT_RIFTBOUND: case FMT_RIFT_2V2: return 4;
  }
  return MAX_PLAYERS;
}

const char *counterName(uint8_t c) {
  switch (c) {
    case C_POISON: return "Poison";
    case C_ENERGY: return "Energy";
    case C_EXPERIENCE: return "Experience";
    case C_RADIATION: return "Radiation";
    case C_SPEED: return "Speed";
    case C_TICKETS: return "Tickets";
  }
  return "?";
}

const char *counterShort(uint8_t c) {
  switch (c) {
    case C_POISON: return "PSN";
    case C_ENERGY: return "NRG";
    case C_EXPERIENCE: return "EXP";
    case C_RADIATION: return "RAD";
    case C_SPEED: return "SPD";
    case C_TICKETS: return "TIX";
  }
  return "?";
}

int counterMax(uint8_t c) {
  switch (c) {
    case C_POISON: return 99;
    case C_SPEED: return 4;
    default: return 255;
  }
}

void applyFormatDefaults() {
  settings.startLife = defaultLife(settings.format);
  settings.numPlayers = defaultPlayers(settings.format);
  markSettingsDirty();
}

bool isArchenemy(int i) { return game.format == FMT_ARCHENEMY && i == 0; }
bool isEmperor(int i) { return game.format == FMT_EMPEROR && (i == 1 || i == 4); }
int team(int i) {
  if (game.format == FMT_EMPEROR) return i < 3 ? 0 : 1;
  if (game.format == FMT_RIFT_2V2) return i < 2 ? 0 : 1;
  return -1;
}

int playerStartLife(int i) {
  if (isArchenemy(i)) return game.startLife + 20;
  return game.startLife;
}

bool hasCustomName(int i) {
  game.name[i][8] = 0;  // names can arrive from other dials: keep them terminated
  return game.name[i][0] != 0;
}

void shortName(int i, char *buf, int len) {
  if (hasCustomName(i)) snprintf(buf, len, "%s", game.name[i]);
  else snprintf(buf, len, "P%d", i + 1);
}

void playerName(int i, char *buf, int len) {
  if (hasCustomName(i)) {
    snprintf(buf, len, "%s", game.name[i]);
  } else if (isArchenemy(i)) {
    snprintf(buf, len, "Archenemy");
  } else if (isEmperor(i)) {
    snprintf(buf, len, "Emperor %c", 'A' + team(i));
  } else if (team(i) >= 0) {
    snprintf(buf, len, "P%d  Team %c", i + 1, 'A' + team(i));
  } else {
    snprintf(buf, len, "Player %d", i + 1);
  }
}

void playerRole(int i, char *buf, int len) {
  if (isArchenemy(i)) snprintf(buf, len, "A");
  else if (isEmperor(i)) snprintf(buf, len, "E");
  else buf[0] = 0;
}

void newGame() {
  uint8_t colors[MAX_PLAYERS];  // players keep their colours and names into the next game
  char names[MAX_PLAYERS][9] = {};
  if (game.magic == GAME_MAGIC) {
    memcpy(colors, game.color, sizeof(colors));
    memcpy(names, game.name, sizeof(names));
  } else {
    memcpy(colors, DEFAULT_COLORS, sizeof(colors));
  }
  Game g;
  memset(&g, 0, sizeof(g));
  g.magic = GAME_MAGIC;
  g.format = settings.format;
  g.numPlayers = clampv<int>(settings.numPlayers, minPlayers(settings.format), maxPlayers(settings.format));
  g.partners = settings.partners;
  g.startLife = settings.startLife;
  g.monarch = -1;
  g.initiative = -1;
  g.turn = 1;
  g.gameId = esp_random();
  g.firstPlayer = -1;
  memcpy(g.color, colors, sizeof(colors));
  memcpy(g.name, names, sizeof(names));
  game = g;
  for (int i = 0; i < MAX_PLAYERS; i++) game.p[i].life = playerStartLife(i);
  undoCount = 0;
  forceUndoPoint = true;
  gameDirty = true;
  dirtyAt = millis();
  changes++;
}

int maxCommanderDamage(int i) {
  int m = 0;
  for (int j = 0; j < game.numPlayers; j++)
    for (int k = 0; k < 2; k++) m = max(m, (int)game.p[i].cmd[j][k]);
  return m;
}

OutReason outReason(int i) {
  const Player &p = game.p[i];
  if (p.conceded) return OUT_CONCEDE;
  if (!isMagic()) return lifeKind() == LK_LIFE && p.life <= 0 ? OUT_LIFE : OUT_NONE;
  if (p.counters[C_POISON] >= POISON_LETHAL) return OUT_POISON;
  if (maxCommanderDamage(i) >= COMMANDER_LETHAL) return OUT_COMMANDER;
  if (p.life <= 0) return OUT_LIFE;
  return OUT_NONE;
}

bool isOut(int i) { return outReason(i) != OUT_NONE; }

// Race formats: seats that reached their goal (Lorcana/Riftbound) or took every prize (Pokemon).
uint8_t raceWinners() {
  uint8_t m = 0;
  int n = game.numPlayers;
  if (game.format == FMT_RIFT_2V2) {
    for (int t = 0; t < 2; t++)
      if (teamScore(t) >= lifeGoal())
        for (int i = 0; i < n; i++)
          if (team(i) == t) m |= 1 << i;
    return m;
  }
  for (int i = 0; i < n; i++) {
    if (isOut(i)) continue;
    if (lifeKind() == LK_RACE && game.p[i].life >= lifeGoal()) m |= 1 << i;
    if (lifeKind() == LK_PRIZES && game.p[i].life <= 0) m |= 1 << i;
  }
  return m;
}

bool gameResult(char *buf, int len) {
  int n = game.numPlayers;
  if (n < 2) return false;
  if (game.format == FMT_RIFT_2V2) {
    uint8_t w = raceWinners();
    bool aOut = isOut(0) && isOut(1), bOut = isOut(2) && isOut(3);
    if ((w & 0x03) || bOut) { snprintf(buf, len, "Team A wins!"); return true; }
    if ((w & 0x0C) || aOut) { snprintf(buf, len, "Team B wins!"); return true; }
    return false;
  }
  if (lifeKind() != LK_LIFE) {
    uint8_t w = raceWinners();
    if (w) {
      char nm[12];
      shortName(__builtin_ctz(w), nm, sizeof(nm));
      snprintf(buf, len, "%s wins!", nm);
      return true;
    }
  }
  if (game.format == FMT_EMPEROR) {
    bool aDown = isOut(1), bDown = isOut(4);
    if (aDown && bDown) { snprintf(buf, len, "Draw"); return true; }
    if (aDown) { snprintf(buf, len, "Team B wins!"); return true; }
    if (bDown) { snprintf(buf, len, "Team A wins!"); return true; }
    return false;
  }
  if (game.format == FMT_ARCHENEMY) {
    if (isOut(0)) { snprintf(buf, len, "Heroes win!"); return true; }
    bool heroAlive = false;
    for (int i = 1; i < n; i++) heroAlive |= !isOut(i);
    if (!heroAlive) { snprintf(buf, len, "Archenemy wins!"); return true; }
    return false;
  }
  int alive = 0, last = -1;
  for (int i = 0; i < n; i++)
    if (!isOut(i)) { alive++; last = i; }
  if (alive == 1) {
    char nm[12];
    shortName(last, nm, sizeof(nm));
    snprintf(buf, len, "%s wins!", nm);
    return true;
  }
  if (alive == 0) { snprintf(buf, len, "Draw"); return true; }
  return false;
}

uint8_t winnerMask() {
  char tmp[24];
  if (!gameResult(tmp, sizeof(tmp))) return 0;
  int n = game.numPlayers;
  if (game.format == FMT_EMPEROR) {
    if (isOut(1) && isOut(4)) return 0;
    return isOut(4) ? 0x07 : 0x38;  // team A = seats 0-2, team B = seats 3-5
  }
  if (game.format == FMT_ARCHENEMY) {
    if (isOut(0)) {
      uint8_t m = 0;
      for (int i = 1; i < n; i++) m |= 1 << i;
      return m;
    }
    return 0x01;
  }
  if (game.format == FMT_RIFT_2V2) {
    uint8_t w = raceWinners();
    if (w & 0x03) return 0x03;
    if (w & 0x0C) return 0x0C;
    return (isOut(2) && isOut(3)) ? 0x03 : 0x0C;
  }
  if (lifeKind() != LK_LIFE) {
    uint8_t w = raceWinners();
    if (w) return 1 << __builtin_ctz(w);
  }
  for (int i = 0; i < n; i++)
    if (!isOut(i)) return 1 << i;
  return 0;
}

void addLife(int i, int delta) {
  if (!delta) return;
  int lo = -999, hi = 9999;
  if (lifeKind() == LK_RACE) lo = 0, hi = 99;
  else if (lifeKind() == LK_PRIZES) lo = 0, hi = game.startLife;
  else if (game.format == FMT_YUGIOH) lo = 0, hi = 32000;
  int nv = clampv<int>(game.p[i].life + delta, lo, hi);
  if (nv == game.p[i].life) return;
  beginEdit();
  game.p[i].life = nv;
}

void addCommanderDamage(int victim, int source, int which, int delta) {
  if (!delta) return;
  uint8_t &d = game.p[victim].cmd[source][which];
  int nv = clampv<int>(d + delta, 0, 99);
  int applied = nv - d;
  if (!applied) return;
  beginEdit();
  d = nv;
  if (settings.cmdHitsLife) game.p[victim].life = clampv<int>(game.p[victim].life - applied, -999, 9999);
}

void addCounter(int i, int c, int delta) {
  if (!delta) return;
  uint8_t &v = game.p[i].counters[c];
  int nv = clampv<int>(v + delta, 0, counterMax(c));
  if (nv == v) return;
  beginEdit();
  v = nv;
}

void editGame() { beginEdit(); }

uint32_t changeCounter() { return changes; }

void adoptRemote(const Game &g) {
  if (memcmp(&g, &game, sizeof(Game)) == 0) return;
  if (g.gameId == game.gameId) pushUndo();  // a new game from another dial clears history instead
  else undoCount = 0;
  game = g;
  forceUndoPoint = true;
  gameDirty = true;
  dirtyAt = millis();
}

void setFirstPlayer(int i) {
  game.firstPlayer = i;
  changes++;
  gameDirty = true;
  dirtyAt = millis();
}

void markUndo() { forceUndoPoint = true; }

bool undo() {
  if (!undoCount) return false;
  undoHead = (undoHead - 1 + UNDO_SLOTS) % UNDO_SLOTS;
  undoCount--;
  game = undoRing[undoHead];
  forceUndoPoint = true;
  changes++;
  gameDirty = true;
  dirtyAt = millis();
  return true;
}

int undoDepth() { return undoCount; }

void markSettingsDirty() {
  settingsDirty = true;
  dirtyAt = millis();
}

void load() {
  prefs.begin("mtgdial", false);
  defaultSettings();
  Settings s;
  if (prefs.getBytes("settings", &s, sizeof(s)) == sizeof(s) && s.magic == SETTINGS_MAGIC) settings = s;
  Game g;
  if (prefs.getBytes("game", &g, sizeof(g)) == sizeof(g) && g.magic == GAME_MAGIC && g.numPlayers >= 1 &&
      g.numPlayers <= MAX_PLAYERS) {
    game = g;
    gameDirty = false;
  } else {
    newGame();
  }
}

void saveIfDirty(uint32_t now) {
  if (!(gameDirty || settingsDirty) || now - dirtyAt < SAVE_DELAY_MS) return;
  if (gameDirty) prefs.putBytes("game", &game, sizeof(game));
  if (settingsDirty) prefs.putBytes("settings", &settings, sizeof(settings));
  gameDirty = settingsDirty = false;
}

}  // namespace mtg
