// Linking dials over ESP-NOW (no router needed). Dials in the same room share one game:
// every change is broadcast as the full game state, the newest revision wins, and each
// dial repeats its state every couple of seconds so late joiners catch up.
#pragma once
#include <stdint.h>

namespace net {
void begin();                 // starts the radio if settings.linkRoom is set
void setRoom(uint8_t room);   // 0 = unlink; otherwise join that room
void loop(uint32_t now);
bool linked();
int peerCount();              // other dials heard in this room recently
bool seatHasDial(int seat);   // a dial (this one or another) claims this seat
// True once after this dial joined a room and adopted the room's game.
bool takeJoinAdopted();
// Counts games adopted from other dials (UI redraws when it changes).
uint32_t adoptions();
// Changes whenever the set of dials in the room (or their seats) changes.
uint32_t peerSignature();
void debugPrint();
// Phone app: run a Wi-Fi access point on the link channel (alongside ESP-NOW).
void setPhoneAp(bool on, const char *ssid, const char *pass);
int phoneClients();           // phones joined to the access point
uint8_t peerSeats();          // bitmask of seats claimed by other dials in the room
void macSuffix(char *buf, int len);  // last two MAC bytes, e.g. "CA68"
}  // namespace net
