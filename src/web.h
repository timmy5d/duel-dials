// Companion app: a Wi-Fi access point on the dial that serves a phone web app, a small
// JSON API to read and change the game, and a running game log.
#pragma once
#include <stdint.h>

namespace web {
void begin();             // restores the saved on/off state
void loop(uint32_t now);  // serve requests, keep the game log up to date
bool enabled();
void setEnabled(bool on);
const char *ssid();
const char *password();
const char *address();    // "192.168.4.1"
int phones();             // phones joined to the access point
void debugDump();         // print the /state JSON (with the log) to Serial
}  // namespace web
