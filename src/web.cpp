#include "web.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "game.h"
#include "input.h"
#include "net.h"
#include "ui.h"
#include "webapp.h"

namespace {

const char *const SSID = "dueldial";
const char *const PASSWORD = "dualdial";
const char *const ADDRESS = "192.168.4.1";
constexpr uint32_t LOG_TICK_MS = 150;
constexpr uint32_t MERGE_MS = 4000;  // quick changes to the same thing become one log line

WebServer server(80);
DNSServer dns;
Preferences prefs;
bool on = false;
bool serving = false;
char ssidBuf[33] = "dueldial";

// ---------------------------------------------------------------- game log
enum LogKind : uint8_t {
  L_NEWGAME, L_LIFE, L_CMD, L_COUNTER, L_CONCEDE, L_OUT, L_MONARCH, L_INITIATIVE, L_TURN, L_SCHEMES, L_WIN,
  L_NAME, L_UNDO
};

struct LogEntry {
  uint32_t ms;     // since the game started
  uint8_t kind;
  int8_t p;        // player
  int8_t src;      // commander source / counter index / out reason / format
  int8_t which;    // commander A/B
  int16_t delta;
  int16_t after;
};

constexpr int LOG_MAX = 80;
LogEntry logBuf[LOG_MAX];
int logCount = 0;
uint32_t logVersion = 1;
uint32_t gameStartMs = 0;
uint32_t lastLogMs = 0;  // when the newest entry last changed
Game snap;
bool haveSnap = false;
uint8_t snapOut[MAX_PLAYERS];
bool snapOver = false;
bool undoPending = false;

void addLog(uint8_t kind, int p, int src, int which, int delta, int after, bool mergeable) {
  uint32_t now = millis();
  if (mergeable && logCount) {
    LogEntry &e = logBuf[logCount - 1];
    if (e.kind == kind && e.p == p && e.src == src && e.which == which && now - lastLogMs < MERGE_MS) {
      e.delta += delta;
      e.after = after;
      lastLogMs = now;
      if (!e.delta) logCount--;  // +1 then -1: nothing happened
      logVersion++;
      return;
    }
  }
  if (logCount == LOG_MAX) {
    memmove(logBuf + 1, logBuf + 2, sizeof(LogEntry) * (LOG_MAX - 2));  // keep the new-game line
    logCount--;
  }
  logBuf[logCount++] = {now - gameStartMs, kind, (int8_t)p, (int8_t)src, (int8_t)which, (int16_t)delta,
                        (int16_t)after};
  lastLogMs = now;
  logVersion++;
}

void takeSnapshot() {
  snap = game;
  for (int i = 0; i < MAX_PLAYERS; i++) snapOut[i] = i < game.numPlayers ? mtg::outReason(i) : 0;
  char tmp[24];
  snapOver = mtg::gameResult(tmp, sizeof(tmp));
  haveSnap = true;
}

// Compare the game with the last snapshot and log what changed (from any dial or the phone).
void updateLog() {
  if (!haveSnap || game.gameId != snap.gameId) {
    logCount = 0;
    gameStartMs = millis();
    addLog(L_NEWGAME, game.numPlayers, game.format, 0, 0, 0, false);
    takeSnapshot();
    return;
  }
  if (!memcmp(&snap, &game, sizeof(Game))) return;
  bool undo = undoPending;
  undoPending = false;
  if (undo) addLog(L_UNDO, -1, 0, 0, 0, 0, false);
  int n = game.numPlayers;
  for (int i = 0; i < n; i++) {
    const Player &a = snap.p[i], &b = game.p[i];
    // Commander damage first: when it also lowers life, the life line follows it.
    for (int j = 0; j < n; j++)
      for (int k = 0; k < 2; k++)
        if (a.cmd[j][k] != b.cmd[j][k]) addLog(L_CMD, i, j, k, b.cmd[j][k] - a.cmd[j][k], b.cmd[j][k], !undo);
    if (a.life != b.life) addLog(L_LIFE, i, 0, 0, b.life - a.life, b.life, !undo);
    for (int c = 0; c < C_COUNT; c++)
      if (a.counters[c] != b.counters[c])
        addLog(L_COUNTER, i, c, 0, b.counters[c] - a.counters[c], b.counters[c], !undo);
    if (a.conceded != b.conceded) addLog(L_CONCEDE, i, 0, 0, 0, b.conceded, false);
    if (strncmp(snap.name[i], game.name[i], 8)) addLog(L_NAME, i, 0, 0, 0, 0, false);
    uint8_t out = mtg::outReason(i);
    if (out && !snapOut[i]) addLog(L_OUT, i, out, 0, 0, 0, false);
  }
  if (snap.monarch != game.monarch) addLog(L_MONARCH, game.monarch, 0, 0, 0, 0, false);
  if (snap.initiative != game.initiative) addLog(L_INITIATIVE, game.initiative, 0, 0, 0, 0, false);
  if (snap.turn != game.turn) addLog(L_TURN, -1, 0, 0, game.turn - snap.turn, game.turn, true);
  if (snap.schemes != game.schemes) addLog(L_SCHEMES, 0, 0, 0, game.schemes - snap.schemes, game.schemes, true);
  char tmp[24];
  bool over = mtg::gameResult(tmp, sizeof(tmp));
  if (over && !snapOver) addLog(L_WIN, -1, 0, 0, 0, mtg::winnerMask(), false);
  takeSnapshot();
}

// ---------------------------------------------------------------- JSON
String out;

void add(const char *fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  out += buf;
}

void addStr(const char *s) {
  out += '"';
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') out += '\\';
    if ((uint8_t)*s >= 0x20) out += *s;
  }
  out += '"';
}

void buildState(uint32_t knownLog) {
  out = "";
  out.reserve(6000);
  char result[24] = "";
  bool over = mtg::gameResult(result, sizeof(result));
  add("{\"id\":%u,\"fmt\":%u,\"sys\":%u,\"kind\":%u,\"n\":%u,\"partners\":%u,\"startLife\":%d,\"goal\":%d,"
      "\"step\":%d,\"turn\":%u,\"dn\":%u,\"storm\":%u,\"schemes\":%u,\"monarch\":%d,\"init\":%d,\"first\":%d,",
      (unsigned)game.gameId, game.format, mtg::systemOf(game.format), mtg::lifeKind(), game.numPlayers,
      game.partners, game.startLife, mtg::lifeGoal(), mtg::lifeStep(), game.turn, game.dayNight, game.storm,
      game.schemes, game.monarch, game.initiative, game.firstPlayer);
  add("\"over\":%d,\"win\":%u,\"result\":", over, mtg::winnerMask());
  addStr(over ? result : "");
  out += ",\"p\":[";
  for (int i = 0; i < game.numPlayers; i++) {
    const Player &p = game.p[i];
    char name[24], role[4];
    mtg::playerName(i, name, sizeof(name));
    mtg::playerRole(i, role, sizeof(role));
    if (i) out += ',';
    out += "{\"name\":";
    addStr(name);
    out += ",\"custom\":";
    addStr(mtg::hasCustomName(i) ? game.name[i] : "");
    add(",\"color\":%u,\"life\":%d,\"out\":%u,\"conceded\":%u,\"team\":%d,\"role\":\"%s\",\"dial\":%d,\"c\":[",
        game.color[i], p.life, mtg::outReason(i), p.conceded, mtg::team(i), role, net::seatHasDial(i) ? 1 : 0);
    for (int c = 0; c < C_COUNT; c++) add(c ? ",%u" : "%u", p.counters[c]);
    out += "],\"cmd\":[";
    for (int j = 0; j < game.numPlayers; j++) add(j ? ",[%u,%u]" : "[%u,%u]", p.cmd[j][0], p.cmd[j][1]);
    out += "]}";
  }
  out += "],";
  char mac[8];
  net::macSuffix(mac, sizeof(mac));
  add("\"dial\":{\"mac\":\"%s\",\"seat\":%u,\"room\":%u,\"linked\":%d,\"peers\":%d,\"peerSeats\":%u,\"theme\":%u,"
      "\"bright\":%u,\"leds\":%u,\"sleep\":%u,\"reverse\":%u,\"face\":%u,\"cmdLife\":%u},",
      mac, settings.mySeat, settings.linkRoom, net::linked(), net::peerCount(), net::peerSeats(), settings.theme,
      settings.brightness, settings.leds, settings.sleepMinutes, settings.reverseDial, settings.facePlayer,
      settings.cmdHitsLife);
  add("\"logVersion\":%u", (unsigned)logVersion);
  if (knownLog != logVersion) {
    out += ",\"log\":[";
    for (int i = 0; i < logCount; i++) {
      const LogEntry &e = logBuf[i];
      add(i ? ",[%u,%u,%d,%d,%d,%d,%d]" : "[%u,%u,%d,%d,%d,%d,%d]", (unsigned)(e.ms / 1000), e.kind, e.p, e.src,
          e.which, e.delta, e.after);
    }
    out += "]";
  }
  out += "}";
}

// ---------------------------------------------------------------- actions
int argi(const char *k, int def = 0) { return server.hasArg(k) ? server.arg(k).toInt() : def; }

bool validPlayer(int p) { return p >= 0 && p < game.numPlayers; }

void cleanName(const String &in, char *dst) {
  static const char OK[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_'!.& ";
  memset(dst, 0, 9);
  int n = 0;
  for (size_t i = 0; i < in.length() && n < 8; i++)
    if (strchr(OK, in[i])) dst[n++] = in[i];
  while (n > 0 && dst[n - 1] == ' ') dst[--n] = 0;
  int start = 0;
  while (dst[start] == ' ') start++;
  if (start) memmove(dst, dst + start, 9 - start);
}

void handleAct() {
  String a = server.arg("a");
  int p = argi("p", -1);
  int d = argi("d");
  bool settingsChanged = false;
  if (a == "life" && validPlayer(p)) {
    mtg::addLife(p, d);
  } else if (a == "cmd" && validPlayer(p)) {
    int s = argi("s", -1), w = constrain(argi("w"), 0, 1);
    if (validPlayer(s) && s != p) mtg::addCommanderDamage(p, s, w, d);
  } else if (a == "ctr" && validPlayer(p)) {
    int c = argi("c", -1);
    if (c >= 0 && c < C_COUNT) mtg::addCounter(p, c, d);
  } else if ((a == "concede" || a == "monarch" || a == "init") && validPlayer(p)) {
    mtg::markUndo();
    mtg::editGame();
    if (a == "concede") game.p[p].conceded = !game.p[p].conceded;
    else if (a == "monarch") game.monarch = game.monarch == p ? -1 : p;
    else game.initiative = game.initiative == p ? -1 : p;
  } else if (a == "turn") {
    mtg::editGame();
    game.turn = constrain((int)game.turn + d, 1, 999);
  } else if (a == "storm") {
    mtg::editGame();
    game.storm = constrain((int)game.storm + d, 0, 999);
  } else if (a == "schemes") {
    mtg::editGame();
    game.schemes = constrain((int)game.schemes + d, 0, 999);
  } else if (a == "dn") {
    mtg::markUndo();
    mtg::editGame();
    game.dayNight = (game.dayNight + 1) % 3;
  } else if (a == "undo") {
    undoPending = mtg::undo();
  } else if (a == "name" && validPlayer(p)) {
    char nm[9];
    cleanName(server.arg("v"), nm);
    mtg::markUndo();
    mtg::editGame();
    memcpy(game.name[p], nm, 9);
  } else if (a == "color" && validPlayer(p)) {
    mtg::markUndo();
    mtg::editGame();
    game.color[p] = constrain(argi("v"), 0, PALETTE_SIZE - 1);
  } else if (a == "spin") {
    ui::whoGoesFirst();
  } else if (a == "new") {
    int f = constrain(argi("f", game.format), 0, FMT_COUNT - 1);
    settings.format = f;
    settings.numPlayers = constrain(argi("n", mtg::defaultPlayers(f)), mtg::minPlayers(f), mtg::maxPlayers(f));
    settings.startLife = mtg::lifeKind(f) == LK_RACE ? 0 : constrain(argi("life", mtg::defaultLife(f)), 1, 32000);
    if (f == FMT_POKEMON) settings.startLife = constrain(settings.startLife, 1, 20);
    settings.partners = argi("partners") ? 1 : 0;
    mtg::markSettingsDirty();
    // Names and colours carry into newGame(), so set them on the current game first.
    for (int i = 0; i < MAX_PLAYERS; i++) {
      char k[4];
      snprintf(k, sizeof(k), "n%d", i);
      if (server.hasArg(k)) cleanName(server.arg(k), game.name[i]);
      snprintf(k, sizeof(k), "c%d", i);
      if (server.hasArg(k)) game.color[i] = constrain(server.arg(k).toInt(), 0, PALETTE_SIZE - 1);
    }
    mtg::newGame();
    ui::startGame();
  } else if (a == "set") {
    String k = server.arg("k");
    int v = argi("v");
    settingsChanged = true;
    if (k == "theme") settings.theme = v ? 1 : 0;
    else if (k == "bright") settings.brightness = constrain(v, 10, 100);
    else if (k == "leds") settings.leds = v ? 1 : 0;
    else if (k == "sleep") settings.sleepMinutes = constrain(v, 0, 60);
    else if (k == "reverse") settings.reverseDial = v ? 1 : 0;
    else if (k == "face") settings.facePlayer = v ? 1 : 0;
    else if (k == "cmdLife") settings.cmdHitsLife = v ? 1 : 0;
    else if (k == "seat") settings.mySeat = constrain(v, 0, (int)game.numPlayers);
    else if (k == "room") {
      settings.linkRoom = constrain(v, 0, 9);
      net::setRoom(settings.linkRoom);
    } else settingsChanged = false;
    if (settingsChanged) mtg::markSettingsDirty();
  } else if (a != "ping") {
    server.send(400, "text/plain", "unknown action");
    return;
  }
  ui::externalChange();
  updateLog();
  buildState(0);
  server.send(200, "application/json", out);
}

void handleState() {
  buildState(server.hasArg("lv") ? server.arg("lv").toInt() : 0);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

void handleApp() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", (const char *)WEBAPP_GZ, WEBAPP_GZ_LEN);
}

// Captive portal: any other host name (phone connectivity checks) is sent to the app.
void handleOther() {
  String host = server.hostHeader();
  if (host != ADDRESS && host.length()) {
    server.sendHeader("Location", String("http://") + ADDRESS + "/", true);
    server.send(302, "text/plain", "");
    return;
  }
  handleApp();
}

void startServing() {
  if (serving) return;
  snprintf(ssidBuf, sizeof(ssidBuf), "%s", SSID);
  net::setPhoneAp(true, ssidBuf, PASSWORD);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  server.on("/", HTTP_GET, handleApp);
  server.on("/state", HTTP_GET, handleState);
  server.on("/act", HTTP_ANY, handleAct);
  server.onNotFound(handleOther);
  server.begin();
  serving = true;
}

void stopServing() {
  if (!serving) return;
  server.stop();
  dns.stop();
  net::setPhoneAp(false, "", "");
  serving = false;
}

}  // namespace

namespace web {

void begin() {
  prefs.begin("dd_web", false);
  on = prefs.getBool("on", false);
  if (on) startServing();
}

void loop(uint32_t now) {
  static uint32_t lastTick = 0;
  if (now - lastTick >= LOG_TICK_MS) {
    lastTick = now;
    updateLog();
  }
  if (!serving) return;
  dns.processNextRequest();
  server.handleClient();
}

bool enabled() { return on; }

void setEnabled(bool v) {
  if (v == on) return;
  on = v;
  prefs.putBool("on", on);
  if (on) startServing();
  else stopServing();
}

const char *ssid() { return ssidBuf; }
const char *password() { return PASSWORD; }
const char *address() { return ADDRESS; }
int phones() { return net::phoneClients(); }

void debugDump() {
  updateLog();
  buildState(0);
  Serial.println(out);
}

}  // namespace web
