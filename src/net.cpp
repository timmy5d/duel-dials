#include "net.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#include "game.h"

namespace {

constexpr uint32_t PKT_MAGIC = 0x4D54474C;  // "MTGL"
constexpr uint8_t PKT_VERSION = 1;
constexpr uint8_t CHANNEL = 1;
constexpr uint32_t HEARTBEAT_MS = 2000;
constexpr uint32_t PEER_TIMEOUT_MS = 7000;
constexpr uint32_t JOIN_WINDOW_MS = 1500;
constexpr uint32_t MIN_SEND_GAP_MS = 30;
constexpr int MAX_PEERS = 8;
const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

enum PktType : uint8_t { PKT_STATE = 1, PKT_HELLO = 2 };

struct __attribute__((packed)) Packet {
  uint32_t magic;
  uint8_t version;
  uint8_t type;
  uint8_t room;
  uint8_t seat;      // sender's seat (0 = none)
  uint32_t rev;      // revision of `game`
  uint8_t origin[6]; // dial that made this revision
  Game game;
};
static_assert(sizeof(Packet) <= 250, "ESP-NOW payload limit");

struct Peer {
  uint8_t mac[6];
  uint8_t seat;
  uint32_t lastSeen;
};

bool radioOn = false;
uint8_t room = 0;
uint8_t selfMac[6];
uint32_t rev = 0;
uint8_t revOrigin[6];
uint32_t lastSentCounter = 0;
uint32_t lastSendAt = 0;
uint32_t lastHeartbeat = 0;
uint32_t replyAt = 0;  // pending reply to a HELLO
bool joining = false;
uint32_t joinStart = 0;
int helloSent = 0;
bool joinAdopted = false;
bool joinAdoptedFlag = false;
uint32_t adoptCount = 0;

Peer peers[MAX_PEERS];
int numPeers = 0;

// Small queue filled by the radio callback and drained in loop().
constexpr int QLEN = 6;
Packet rxq[QLEN];
uint8_t rxMac[QLEN][6];
volatile int qHead = 0, qTail = 0;
portMUX_TYPE qMux = portMUX_INITIALIZER_UNLOCKED;

void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != (int)sizeof(Packet)) return;
  const Packet *p = (const Packet *)data;
  if (p->magic != PKT_MAGIC || p->version != PKT_VERSION) return;
  portENTER_CRITICAL(&qMux);
  int next = (qHead + 1) % QLEN;
  if (next != qTail) {
    memcpy(&rxq[qHead], data, sizeof(Packet));
    memcpy(rxMac[qHead], mac, 6);
    qHead = next;
  }
  portEXIT_CRITICAL(&qMux);
}

bool popPacket(Packet &p, uint8_t *mac) {
  bool ok = false;
  portENTER_CRITICAL(&qMux);
  if (qTail != qHead) {
    memcpy(&p, &rxq[qTail], sizeof(Packet));
    memcpy(mac, rxMac[qTail], 6);
    qTail = (qTail + 1) % QLEN;
    ok = true;
  }
  portEXIT_CRITICAL(&qMux);
  return ok;
}

void send(uint8_t type) {
  if (!radioOn || !room) return;
  static Packet p;
  p.magic = PKT_MAGIC;
  p.version = PKT_VERSION;
  p.type = type;
  p.room = room;
  p.seat = settings.mySeat;
  p.rev = rev;
  memcpy(p.origin, revOrigin, 6);
  p.game = game;
  esp_now_send(BCAST, (const uint8_t *)&p, sizeof(p));
  lastSendAt = millis();
  if (type == PKT_STATE) lastHeartbeat = lastSendAt;
}

void notePeer(const uint8_t *mac, uint8_t seat, uint32_t now) {
  for (int i = 0; i < numPeers; i++) {
    if (!memcmp(peers[i].mac, mac, 6)) {
      peers[i].seat = seat;
      peers[i].lastSeen = now;
      return;
    }
  }
  if (numPeers < MAX_PEERS) {
    memcpy(peers[numPeers].mac, mac, 6);
    peers[numPeers].seat = seat;
    peers[numPeers].lastSeen = now;
    numPeers++;
  }
}

void prunePeers(uint32_t now) {
  for (int i = 0; i < numPeers;) {
    if (now - peers[i].lastSeen > PEER_TIMEOUT_MS) peers[i] = peers[--numPeers];
    else i++;
  }
}

bool newer(uint32_t r, const uint8_t *origin) {
  if (r != rev) return r > rev;
  return memcmp(origin, revOrigin, 6) > 0;
}

void adopt(const Packet &p) {
  rev = p.rev;
  memcpy(revOrigin, p.origin, 6);
  mtg::adoptRemote(p.game);
  adoptCount++;
  lastSentCounter = mtg::changeCounter();  // don't echo what we just received
}

// Wi-Fi is shared by the dial link (ESP-NOW on the station side) and the phone app
// (a soft access point). Both stay on CHANNEL so they can run together.
bool apWanted = false;
bool apUp = false;
char apSsid[33] = "";
char apPass[65] = "";

void applyMode() {
  bool link = room != 0;
  wifi_mode_t want = apWanted ? (link ? WIFI_AP_STA : WIFI_AP) : (link ? WIFI_STA : WIFI_OFF);
  if (WiFi.getMode() != want) WiFi.mode(want);
  if (apWanted && !apUp) {
    WiFi.softAP(apSsid, apPass, CHANNEL, 0, 4);
    apUp = true;
  } else if (!apWanted) {
    apUp = false;
  }
}

void startRadio() {
  if (radioOn) return;
  applyMode();
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("esp_now_init failed");
    return;
  }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BCAST, 6);
  peer.channel = CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
  esp_now_register_recv_cb(onRecv);
  WiFi.macAddress(selfMac);
  radioOn = true;
}

void stopRadio() {
  if (!radioOn) return;
  esp_now_deinit();
  radioOn = false;
  applyMode();
}

}  // namespace

namespace net {

void begin() {
  memset(revOrigin, 0, 6);
  if (settings.linkRoom) setRoom(settings.linkRoom);
}

void setRoom(uint8_t r) {
  room = r;
  numPeers = 0;
  qTail = qHead;
  if (!room) {
    stopRadio();
    joining = false;
    return;
  }
  startRadio();
  // Join: ask the room for its game and adopt the first answer.
  joining = true;
  joinAdopted = false;
  joinStart = millis();
  helloSent = 0;
  rev = 0;
  memcpy(revOrigin, selfMac, 6);
  lastSentCounter = mtg::changeCounter();
}

bool linked() { return radioOn && room; }

void setPhoneAp(bool on, const char *ssid, const char *pass) {
  if (on) {
    bool changed = strcmp(ssid, apSsid) || strcmp(pass, apPass);
    snprintf(apSsid, sizeof(apSsid), "%s", ssid);
    snprintf(apPass, sizeof(apPass), "%s", pass);
    if (changed && apUp) {
      WiFi.softAPdisconnect(false);
      apUp = false;
    }
  } else if (apUp) {
    WiFi.softAPdisconnect(false);
    apUp = false;
  }
  apWanted = on;
  applyMode();
  if (on && room && radioOn) esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE);
}

int phoneClients() { return apUp ? WiFi.softAPgetStationNum() : 0; }

uint8_t peerSeats() {
  uint8_t m = 0;
  for (int i = 0; i < numPeers; i++)
    if (peers[i].seat) m |= 1 << (peers[i].seat - 1);
  return m;
}

void macSuffix(char *buf, int len) {
  uint64_t mac = ESP.getEfuseMac();  // base (station) MAC, byte 0 in the low bits
  snprintf(buf, len, "%02X%02X", (unsigned)((mac >> 32) & 0xFF), (unsigned)((mac >> 40) & 0xFF));
}

int peerCount() { return numPeers; }

bool seatHasDial(int seat) {
  if (!linked()) return false;
  if (settings.mySeat == seat + 1) return true;
  for (int i = 0; i < numPeers; i++)
    if (peers[i].seat == seat + 1) return true;
  return false;
}

uint32_t adoptions() { return adoptCount; }

uint32_t peerSignature() {
  uint32_t sig = linked() ? 0x80000000u + numPeers : 0;
  for (int i = 0; i < numPeers; i++) sig += (uint32_t)peers[i].seat << (4 * (i % 7));
  return sig;
}

bool takeJoinAdopted() {
  bool f = joinAdoptedFlag;
  joinAdoptedFlag = false;
  return f;
}

void loop(uint32_t now) {
  if (!linked()) return;

  Packet p;
  uint8_t mac[6];
  while (popPacket(p, mac)) {
    if (p.room != room || !memcmp(mac, selfMac, 6)) continue;
    notePeer(mac, p.seat, now);
    if (p.type == PKT_HELLO) {
      if (!joining && !replyAt) replyAt = now + 5 + esp_random() % 60;  // stagger replies
      continue;
    }
    if (joining) {
      if (!joinAdopted || newer(p.rev, p.origin)) {
        adopt(p);
        joinAdopted = true;
        joinAdoptedFlag = true;
      }
    } else if (newer(p.rev, p.origin)) {
      adopt(p);
    }
  }

  if (joining) {
    if (helloSent < 3 && now - joinStart >= (uint32_t)helloSent * 400) {
      send(PKT_HELLO);
      helloSent++;
    }
    if (now - joinStart >= JOIN_WINDOW_MS) {
      joining = false;
      if (!joinAdopted) {  // nobody here yet: our game becomes the room's game
        rev = 1;
        memcpy(revOrigin, selfMac, 6);
        send(PKT_STATE);
      }
    }
    return;
  }

  // Local change -> new revision, broadcast (rate-limited so dial spins stay smooth).
  if (mtg::changeCounter() != lastSentCounter && now - lastSendAt >= MIN_SEND_GAP_MS) {
    lastSentCounter = mtg::changeCounter();
    rev++;
    memcpy(revOrigin, selfMac, 6);
    send(PKT_STATE);
  } else if (replyAt && now >= replyAt) {
    replyAt = 0;
    send(PKT_STATE);
  } else if (now - lastHeartbeat >= HEARTBEAT_MS) {
    send(PKT_STATE);
  }
  prunePeers(now);
}

void debugPrint() {
  Serial.printf("link room=%u radio=%d joining=%d rev=%u peers=%d mac=%02X%02X\n", room, radioOn, joining,
                (unsigned)rev, numPeers, selfMac[4], selfMac[5]);
  for (int i = 0; i < numPeers; i++)
    Serial.printf("  peer %02X:%02X seat=%u age=%ums\n", peers[i].mac[4], peers[i].mac[5], peers[i].seat,
                  (unsigned)(millis() - peers[i].lastSeen));
}

}  // namespace net
