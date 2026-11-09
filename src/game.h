// Magic: The Gathering game state, rules and persistence.
#pragma once
#include <stdint.h>

constexpr int MAX_PLAYERS = 6;
constexpr int POISON_LETHAL = 10;
constexpr int COMMANDER_LETHAL = 21;

enum Format : uint8_t {
  FMT_STANDARD,   // 20 life, 2+ players
  FMT_COMMANDER,  // 40 life, commander damage
  FMT_ARCHENEMY,  // Player 1 is the Archenemy (start life + 20), others are heroes
  FMT_EMPEROR,    // 6 players, two teams of three, emperor sits in the middle
  FMT_YUGIOH,     // 8000 LP, 2-4 duelists
  FMT_LORCANA,    // lore race to 20, 2-4 players
  FMT_POKEMON,    // 6 prize cards each, take them all to win, 2 players
  FMT_RIFTBOUND,  // points race to 8, 1v1 or free-for-all (2-4)
  FMT_RIFT_2V2,   // Riftbound 2v2: team points race to 11
  FMT_COUNT
};

// Card games on offer; each format belongs to one.
enum GameSystem : uint8_t { SYS_MAGIC, SYS_YUGIOH, SYS_LORCANA, SYS_POKEMON, SYS_RIFTBOUND, SYS_COUNT };

// What the big number tracks.
enum LifeKind : uint8_t {
  LK_LIFE,    // counts down, out at 0 (Magic, Yu-Gi-Oh!)
  LK_RACE,    // counts up, first to the goal wins (Lorcana lore, Riftbound points)
  LK_PRIZES,  // counts down, first to 0 wins (Pokemon prize cards)
};

// Per-player counters. Order is the display order in the player menu.
enum Counter : uint8_t {
  C_POISON,      // infect, toxic, poisonous, proliferate; 10 = loss
  C_ENERGY,
  C_EXPERIENCE,
  C_RADIATION,   // mill that many at precombat main, lose 1 life per nonland
  C_SPEED,       // Start your engines! (0-4)
  C_TICKETS,
  C_COUNT
};

enum DayNight : uint8_t { DN_NONE, DN_DAY, DN_NIGHT };

enum OutReason : uint8_t { OUT_NONE, OUT_LIFE, OUT_POISON, OUT_COMMANDER, OUT_CONCEDE };

struct Player {
  int16_t life;
  uint8_t cmd[MAX_PLAYERS][2];  // commander damage received from player j's commander k
  uint8_t counters[C_COUNT];
  uint8_t conceded;
};

struct Game {
  uint32_t magic;
  uint8_t format;
  uint8_t numPlayers;
  uint8_t partners;  // track a second (partner/background) commander per player
  int16_t startLife;
  Player p[MAX_PLAYERS];
  int8_t monarch;     // -1 = none
  int8_t initiative;  // -1 = none
  uint8_t dayNight;
  uint16_t storm;
  uint16_t turn;
  uint16_t schemes;   // Archenemy: schemes set in motion
  uint32_t gameId;    // random per New Game; lets linked dials notice a new game
  int8_t firstPlayer; // chosen by the spinner, -1 = not chosen yet
  uint8_t color[MAX_PLAYERS];  // palette index per player (kept across new games)
  char name[MAX_PLAYERS][9];   // custom name, up to 8 chars; "" = default "Player N"
};

constexpr int PALETTE_SIZE = 20;

struct Settings {
  uint32_t magic;
  uint8_t theme;  // 0 = dark, 1 = light
  uint8_t brightness;  // 10..100
  uint8_t reverseDial;
  uint8_t leds;
  uint8_t facePlayer;     // rotate centre readout toward the focused player
  uint8_t cmdHitsLife;    // commander damage also reduces life
  uint8_t format;         // setup choices for the next New Game
  uint8_t numPlayers;
  uint8_t partners;
  int16_t startLife;
  uint8_t sleepMinutes;   // 0 = never
  uint8_t linkRoom;       // 0 = not linked, 1..9 = room shared with other dials
  uint8_t mySeat;         // 0 = none, 1..6 = the player this dial belongs to
};

extern Game game;
extern Settings settings;

namespace mtg {

const char *formatName(uint8_t f);
uint8_t systemOf(uint8_t f);
const char *systemName(uint8_t s);
bool isMagic();                // current game is Magic (commander damage, poison, counters ...)
LifeKind lifeKind(uint8_t f);
LifeKind lifeKind();           // for the current game
int lifeGoal();                // LK_RACE: points to win (per player, or per team in 2v2)
int lifeStep();                // dial step for life (100 in Yu-Gi-Oh!)
const char *lifeName();        // "Life", "LP", "Lore", "Prizes", "Points"
int teamScore(int t);          // Riftbound 2v2: the team's combined points
int defaultLife(uint8_t f);
int defaultPlayers(uint8_t f);
int minPlayers(uint8_t f);
int maxPlayers(uint8_t f);
const char *counterName(uint8_t c);
const char *counterShort(uint8_t c);
int counterMax(uint8_t c);

void newGame();  // from current settings
void applyFormatDefaults();  // reset setup values when format changes

int playerStartLife(int i);
bool isArchenemy(int i);
bool isEmperor(int i);
int team(int i);  // Emperor: 0 or 1; otherwise -1
void playerName(int i, char *buf, int len);  // custom name, or "Player 1", "Archenemy", "Emperor A" ...
void shortName(int i, char *buf, int len);   // custom name, or "P1"
bool hasCustomName(int i);
void playerRole(int i, char *buf, int len);  // short role tag or ""

OutReason outReason(int i);
bool isOut(int i);
int maxCommanderDamage(int i);
// Returns true when the game is decided; writes the result into buf.
bool gameResult(char *buf, int len);
// Bitmask of the winning seats once the game is decided (0 = undecided or a draw).
uint8_t winnerMask();

// Mutators. Each records an undo point when a new edit burst starts.
void addLife(int i, int delta);
void addCommanderDamage(int victim, int source, int which, int delta);
void addCounter(int i, int c, int delta);

// Call before directly modifying `game` so the change is saved and undoable.
void editGame();
void markUndo();  // force a new undo point before the next edit
// Bumped on every local change; the link layer broadcasts when it moves.
uint32_t changeCounter();
// Replace the game with one received from another dial (undoable, not re-broadcast).
void adoptRemote(const Game &g);
// Record the spinner's pick so linked dials land on the same player.
void setFirstPlayer(int i);
bool undo();
int undoDepth();

void load();
void saveIfDirty(uint32_t now);
void markSettingsDirty();

}  // namespace mtg
