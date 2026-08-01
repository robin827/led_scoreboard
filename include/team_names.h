/**
 * team_names.h - Team & player names (NVS-backed)
 *
 * Local-only storage for team/player names typed in the portal. Read by
 * led.h for the pre-match scrolling marquee, pushed to the central server
 * over WebSocket, and pushed directly to Firebase in Firebase mode.
 */

#pragma once
#include <Arduino.h>
#include <Preferences.h>

namespace TeamNames {

static constexpr size_t MAX_LEN = 24;

struct Names {
  char teamA[MAX_LEN + 1]    = "";
  char teamB[MAX_LEN + 1]    = "";
  char playerA1[MAX_LEN + 1] = "";
  char playerA2[MAX_LEN + 1] = "";
  char playerB1[MAX_LEN + 1] = "";
  char playerB2[MAX_LEN + 1] = "";
};

static Names       _names;
static Preferences _prefs;

inline void init() {
  _prefs.begin("teams", true);
  strlcpy(_names.teamA,    _prefs.getString("teamA",    "").c_str(), sizeof(_names.teamA));
  strlcpy(_names.teamB,    _prefs.getString("teamB",    "").c_str(), sizeof(_names.teamB));
  strlcpy(_names.playerA1, _prefs.getString("playerA1", "").c_str(), sizeof(_names.playerA1));
  strlcpy(_names.playerA2, _prefs.getString("playerA2", "").c_str(), sizeof(_names.playerA2));
  strlcpy(_names.playerB1, _prefs.getString("playerB1", "").c_str(), sizeof(_names.playerB1));
  strlcpy(_names.playerB2, _prefs.getString("playerB2", "").c_str(), sizeof(_names.playerB2));
  _prefs.end();
}

inline const Names& get() { return _names; }

inline void set(const char* teamA, const char* teamB,
                 const char* playerA1, const char* playerA2,
                 const char* playerB1, const char* playerB2) {
  strlcpy(_names.teamA,    teamA,    sizeof(_names.teamA));
  strlcpy(_names.teamB,    teamB,    sizeof(_names.teamB));
  strlcpy(_names.playerA1, playerA1, sizeof(_names.playerA1));
  strlcpy(_names.playerA2, playerA2, sizeof(_names.playerA2));
  strlcpy(_names.playerB1, playerB1, sizeof(_names.playerB1));
  strlcpy(_names.playerB2, playerB2, sizeof(_names.playerB2));

  _prefs.begin("teams", false);
  _prefs.putString("teamA",    _names.teamA);
  _prefs.putString("teamB",    _names.teamB);
  _prefs.putString("playerA1", _names.playerA1);
  _prefs.putString("playerA2", _names.playerA2);
  _prefs.putString("playerB1", _names.playerB1);
  _prefs.putString("playerB2", _names.playerB2);
  _prefs.end();

  Serial.printf("[Teams] Saved: A=\"%s\" B=\"%s\"\n", _names.teamA, _names.teamB);
}

// Only team names gate the pre-match marquee — player names are metadata only.
inline bool hasAnyTeamName() { return _names.teamA[0] != '\0' || _names.teamB[0] != '\0'; }

} // namespace TeamNames
