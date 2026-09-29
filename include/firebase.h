/**
 * firebase.h - Lecture Firebase Realtime Database
 */

#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "config.h"
#include "score.h"
#include "team_names.h"

namespace Firebase {

static Preferences _prefs;
static String   _channelCache      = "";
static bool     _channelLoaded     = false;
static uint16_t _pollIntervalSec   = 3;
// See setChannel()/applyPendingChannel() below for why a channel change
// doesn't take effect on _channelCache immediately.
static String         _pendingChannel       = "";
static volatile bool  _channelChangePending = false;

// Shared SSL client — reused across read and write to avoid memory leaks
static WiFiClientSecure* _client = nullptr;

inline WiFiClientSecure* _getClient() {
  if (_client == nullptr) {
    _client = new WiFiClientSecure();
    _client->setInsecure();
    _client->setTimeout(10);
  }
  return _client;
}

inline void _resetClient() {
  delete _client;
  _client = nullptr;
}

// ── Channel (match ID) ────────────────────────────────────────────────────

// Persists the new channel immediately, but deliberately does NOT update
// _channelCache here — this runs on the portal's HTTP handler (Core 1),
// while getChannel() is read from the Firebase task's own thread (Core 0),
// at essentially any point in the middle of a poll tick. Applying the
// change immediately let a tick already in progress pick up the NEW
// channel's data (since getChannel() would suddenly return it mid-tick)
// while still using the OLD channel's freshScoreRead/freshTimerRead/
// lastSeenRemoteTimerType bookkeeping — e.g. an ongoing timeout on the new
// channel would be seen as "some timer we don't recognize yet" and started
// at full duration (apply()'s path) for one tick, before the *next* tick's
// resync properly reset that bookkeeping and corrected it to the real
// remaining time. Reported live as the board flashing the wrong (full)
// countdown, then a bare score, then finally the correct countdown, all
// within about a second of switching channels.
// applyPendingChannel() — called by the Firebase task itself, atomically
// with that same bookkeeping reset (see main.cpp's requestFirebaseResync()
// handling) — is what actually makes the new channel visible to
// getChannel(), so no read can ever see "new channel, stale bookkeeping".
inline void setChannel(const String& matchId) {
  _pendingChannel       = matchId;
  _channelChangePending = true;
  _prefs.begin("firebase", false);
  _prefs.putString("channel", matchId);
  _prefs.end();
  Serial.printf("[Firebase] Channel set: %s\n", matchId.c_str());
}

// Called by the Firebase task (main.cpp's _firebaseRun()) as part of
// handling a resync request. No-op (returns false) if no channel change is
// pending, so it's safe to call on every resync regardless of cause.
inline bool applyPendingChannel() {
  if (!_channelChangePending) return false;
  _channelChangePending = false;
  _channelCache  = _pendingChannel;
  _channelLoaded = true;
  return true;
}

inline void loadPollInterval() {
  _prefs.begin("firebase", true);
  _pollIntervalSec = _prefs.getUShort("pollInt", 3);
  _prefs.end();
}

inline uint32_t getPollIntervalMs()  { return (uint32_t)_pollIntervalSec * 1000UL; }
inline uint16_t getPollIntervalSec() { return _pollIntervalSec; }

inline void setPollInterval(uint16_t secs) {
  _pollIntervalSec = secs;
  _prefs.begin("firebase", false);
  _prefs.putUShort("pollInt", secs);
  _prefs.end();
  Serial.printf("[Firebase] Poll interval: %ds\n", secs);
}

inline String getChannel() {
  if (!_channelLoaded) {
    _prefs.begin("firebase", true);
    _channelCache = _prefs.getString("channel", "");
    _prefs.end();
    _channelLoaded = true;
  }
  return _channelCache;
}

// ── Parse game_settings.set_mode into the firmware's own format enum ──────
// set_mode is a free-form string owned jointly by whichever writer last set
// it (this board, the manager server's firebaseBridge.js, or the Fwango
// bridge in TournamentLiveScores — see that project's CLAUDE.md) — values
// seen in practice include plain digit strings ("1"/"2"/"3", this board's
// own convention: total sets in the match) and labels like "Best of 3".
// Mirrors TournamentLiveScores/public/index.html's formatLabel()/
// setsToWinMatch() parsing so all readers agree on what a given string means.
inline uint8_t _parseSetMode(const String& raw) {
  String text = raw;
  text.trim();
  String lower = text;
  lower.toLowerCase();
  if (text == "1" || lower.indexOf("1 set") >= 0) return 0;
  if (text == "2" || lower.indexOf("2 set") >= 0) return 1;
  return 2; // default: Best of 3 (also covers "3", "Best of 3", unrecognized)
}

// Parses an RFC 1123 HTTP "Date" header ("Tue, 28 Sep 2026 20:14:00 GMT",
// always GMT per spec) into epoch milliseconds. Returns 0 on any parse
// failure. Hand-rolled rather than strptime/timegm since neither is
// reliably available across the ESP32 Arduino core's libc — used to learn
// the current wall-clock time from Firebase's own response instead of
// needing NTP (which needs UDP/123, not guaranteed open on venue WiFi; this
// reuses the HTTPS connection every Firebase read already makes).
// Returns int64_t, not long: `long` is 32-bit on ESP32 (same as `int`), and
// an epoch millisecond value (~1.7e12) overflows that long before the
// function even returns it — this only needs to fit an epoch in *seconds*
// (~1.7e9, fine in 32 bits), not milliseconds.
inline int64_t _parseHttpDateToEpochMs(const String& date) {
  if (date.length() < 29) return 0; // "Tue, 28 Sep 2026 20:14:00 GMT" == 29 chars
  int day  = date.substring(5, 7).toInt();
  String mon = date.substring(8, 11);
  int year = date.substring(12, 16).toInt();
  int hour = date.substring(17, 19).toInt();
  int mins = date.substring(20, 22).toInt();
  int sec  = date.substring(23, 25).toInt();

  static const char* MONTHS = "JanFebMarAprMayJunJulAugSepOctNovDec";
  int monIdx = -1;
  for (int i = 0; i < 12; i++) {
    if (mon[0] == MONTHS[i * 3] && mon[1] == MONTHS[i * 3 + 1] && mon[2] == MONTHS[i * 3 + 2]) {
      monIdx = i;
      break;
    }
  }
  if (monIdx < 0 || day < 1 || day > 31 || year < 2020) return 0;

  static const int16_t CUM_DAYS[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  int64_t days = 0;
  for (int y = 1970; y < year; y++) {
    bool yLeap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    days += yLeap ? 366 : 365;
  }
  days += CUM_DAYS[monIdx];
  if (monIdx > 1 && leap) days += 1;
  days += (day - 1);

  int64_t epochSec = days * 86400LL + hour * 3600LL + mins * 60LL + sec;
  return epochSec * 1000LL;
}

// ── Lecture du score depuis Firebase ──────────────────────────────────────

inline bool readScore(Score& score) {
  String channel = getChannel();
  if (channel.isEmpty()) {
    Serial.println("[Firebase] No channel configured");
    return false;
  }

  if (!WiFi.isConnected()) {
    Serial.println("[Firebase] Not connected");
    return false;
  }
  
  // Vérifie que l'IP STA est valide (pas 0.0.0.0)
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) {
    Serial.println("[Firebase] No valid IP");
    return false;
  }

  HTTPClient http;
  http.setTimeout(10000);
  http.setReuse(false);

  String url = String(FIREBASE_DATABASE_URL)
    + "/match-" + channel + ".json?shallow=false";

  if (!http.begin(*_getClient(), url)) {
    Serial.println("[Firebase] Failed to begin HTTP (DNS error?)");
    _resetClient();
    return false;
  }

  int code = http.GET();

  if (code != 200) {
    Serial.printf("[Firebase] Failed to read match: %d\n", code);
    http.end();
    if (code < 0) {
      Serial.println("[Firebase] Network/SSL/DNS error, recreating client");
      _resetClient();
    }
    return false;
  }

  String payload = http.getString();
  http.end();

  // Parse active_set
  int idxActiveSet = payload.indexOf("\"active_set\":");
  if (idxActiveSet < 0) {
    Serial.println("[Firebase] No active_set found");
    return false;
  }
  int activeSet = payload.substring(idxActiveSet + 13).toInt();
  
  if (activeSet < 1) {
    Serial.println("[Firebase] Invalid active_set");
    return false;
  }

  // Parse le set actif
  String setKey = "\"set_" + String(activeSet) + "\":{";
  int idxSet = payload.indexOf(setKey);
  if (idxSet < 0) {
    Serial.printf("[Firebase] Set %d not found\n", activeSet);
    return false;
  }

  int searchStart = idxSet;
  int idxA = payload.indexOf("\"team_a_score\":", searchStart);
  int idxB = payload.indexOf("\"team_b_score\":", searchStart);

  if (idxA < 0 || idxB < 0) {
    Serial.println("[Firebase] Scores not found");
    return false;
  }

  score.scoreA = payload.substring(idxA + 15).toInt();
  score.scoreB = payload.substring(idxB + 15).toInt();

  // Parse game_settings.win_points and hardcap
  int idxWP = payload.indexOf("\"win_points\":");
  if (idxWP >= 0) {
    int valStart = idxWP + 13;
    while (valStart < (int)payload.length() && (payload[valStart] == ' ' || payload[valStart] == '"')) valStart++;
    int wp = payload.substring(valStart).toInt();
    if (wp >= 5 && wp <= 99) score.winPoints = wp;
  }
  int idxHC = payload.indexOf("\"hardcap\":");
  if (idxHC >= 0) {
    int valStart = idxHC + 10;
    while (valStart < (int)payload.length() && (payload[valStart] == ' ' || payload[valStart] == '"')) valStart++;
    int hc = payload.substring(valStart).toInt();
    if (hc == 0 || (hc >= 5 && hc <= 99)) score.hardcap = (uint8_t)hc;
  }
  // Parse game_settings.set_mode (this board's own "format": 0=BO1, 1=2 sets,
  // 2=BO3) — same node other tools call "Game Mode"/"set_mode", see
  // _parseSetMode() above for why the values aren't a simple number match.
  int idxSM = payload.indexOf("\"set_mode\":");
  if (idxSM >= 0) {
    int valStart = idxSM + 11;
    while (valStart < (int)payload.length() && payload[valStart] == ' ') valStart++;
    String smVal;
    if (valStart < (int)payload.length() && payload[valStart] == '"') {
      int q2 = payload.indexOf('"', valStart + 1);
      if (q2 > valStart) smVal = payload.substring(valStart + 1, q2);
    } else {
      int end = valStart;
      while (end < (int)payload.length() && payload[end] != ',' && payload[end] != '}') end++;
      smVal = payload.substring(valStart, end);
      smVal.trim();
    }
    if (smVal.length() > 0) score.format = _parseSetMode(smVal);
  }

  // Parse starting_server: "a"/"b" → Team A (firstServer=0), "c"/"d" → Team B (firstServer=1)
  int idxSS = payload.indexOf("\"starting_server\":", searchStart);
  if (idxSS >= 0) {
    int q1 = payload.indexOf('"', idxSS + 18) + 1;
    int q2 = payload.indexOf('"', q1);
    if (q1 > 0 && q2 > q1) {
      String sv = payload.substring(q1, q2);
      score.firstServer = (sv == "a" || sv == "b") ? 0 : 1;
    }
  }

  // Compte les sets gagnés (+ capture leur score final dans histA/histB)
  score.setA = 0;
  score.setB = 0;

  for (int i = 1; i < activeSet; i++) {
    String prevSetKey = "\"set_" + String(i) + "\":{";
    int prevIdx = payload.indexOf(prevSetKey);
    if (prevIdx >= 0) {
      int prevIdxA = payload.indexOf("\"team_a_score\":", prevIdx);
      int prevIdxB = payload.indexOf("\"team_b_score\":", prevIdx);

      if (prevIdxA >= 0 && prevIdxB >= 0) {
        int prevScoreA = payload.substring(prevIdxA + 15).toInt();
        int prevScoreB = payload.substring(prevIdxB + 15).toInt();

        if (prevScoreA > prevScoreB) {
          score.setA++;
        } else if (prevScoreB > prevScoreA) {
          score.setB++;
        }

        // Without this, `score` (the `db` passed in by firebaseTask) keeps
        // whatever histA/histB it started with — which, on a set transition
        // driven by an external writer rather than this board's own
        // nextSet(), is the PRE-transition (stale, possibly empty) history.
        // applyFromDatabase() then replaces currentScore wholesale with that
        // stale history, and this board's own next writeScore() rebuilds
        // score/set_<N> in Firebase straight from it — silently overwriting
        // the real completed-set score with 0 (or whatever was left in the
        // array). Capturing it here, from the exact same parse already used
        // to count setA/setB, keeps the board's history in sync with
        // whichever writer actually created the set.
        if (i - 1 < 3) {
          score.histA[i - 1] = (uint8_t)constrain(prevScoreA, 0, 99);
          score.histB[i - 1] = (uint8_t)constrain(prevScoreB, 0, 99);
        }
      }
    }
  }

  Serial.printf("[Firebase] Read OK: A=%d B=%d (sets %d-%d)\n",
    score.scoreA, score.scoreB, score.setA, score.setB);

  return true;
}

// ── Ecriture du score vers Firebase ───────────────────────────────────────
// starting_server/starting_receiver for the active set must be written in
// THIS SAME PATCH, not a separate request — writeScore's PATCH replaces the
// entire "score" node wholesale (Firebase PATCH semantics: a top-level key's
// value is written as-is, not deep-merged), so if this call only ever wrote
// team_a_score/team_b_score, every score update would silently wipe whatever
// starting_server the board (or anything else) had previously written for
// the active set — leaving the live-scores page's serve indicator missing
// on every ordinary point, not just occasionally. Same bug class already
// found and fixed in the manager server's firebaseBridge.js (see that
// file's "used to be two separate PATCH requests" note); this is the
// FIREBASE-mode (direct board→Firebase) equivalent, previously unfixed.

inline bool writeScore(const Score& score) {
  String channel = getChannel();
  if (channel.isEmpty()) {
    Serial.println("[Firebase] No channel configured");
    return false;
  }

  if (!WiFi.isConnected()) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) return false;

  HTTPClient http;
  http.setTimeout(5000);
  http.setReuse(false);

  String url = String(FIREBASE_DATABASE_URL) + "/match-" + channel + ".json";

  if (!http.begin(*_getClient(), url)) {
    Serial.println("[Firebase] Write: failed to begin HTTP");
    _resetClient();
    return false;
  }

  http.addHeader("Content-Type", "application/json");

  // Build payload: active_set + all historical sets + current set
  JsonDocument doc;
  int activeSet = score.setA + score.setB + 1;
  doc["active_set"] = activeSet;

  for (int i = 0; i < activeSet - 1; i++) {
    String key = "set_" + String(i + 1);
    doc["score"][key]["team_a_score"] = score.histA[i];
    doc["score"][key]["team_b_score"] = score.histB[i];
  }
  String currentKey = "set_" + String(activeSet);
  doc["score"][currentKey]["team_a_score"] = score.scoreA;
  doc["score"][currentKey]["team_b_score"] = score.scoreB;
  doc["score"][currentKey]["starting_server"]   = (score.firstServer == 0) ? "a" : "c";
  doc["score"][currentKey]["starting_receiver"] = (score.firstServer == 0) ? "c" : "a";

  String payload;
  serializeJson(doc, payload);

  int code = http.PATCH(payload);
  http.end();

  if (code < 0) {
    Serial.printf("[Firebase] Write error: %d, recreating client\n", code);
    _resetClient();
    return false;
  }

  Serial.printf("[Firebase] Write OK: A=%d B=%d (set %d, code %d)\n",
    score.scoreA, score.scoreB, activeSet, code);
  return code == 200;
}

// ── Write starting_server + starting_receiver for the active set in one PATCH ──
// writeScore() above now also carries these fields on every score push, so
// this is only still needed for the standalone case: firstServer changes
// (or a new set starts) without scoreA/scoreB/setA/setB also changing on
// that same tick — main.cpp gates the call on exactly that condition.
// firstServer=0 (Team A / yellow): server="a", receiver="c"
// firstServer=1 (Team B / blue):   server="c", receiver="a"

inline bool writeFirstServer(const Score& score) {
  String channel = getChannel();
  if (channel.isEmpty()) return false;
  if (!WiFi.isConnected()) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) return false;

  int activeSet = score.setA + score.setB + 1;
  String url = String(FIREBASE_DATABASE_URL)
    + "/match-" + channel + "/score/set_" + String(activeSet) + ".json";

  const char* serverVal   = (score.firstServer == 0) ? "a" : "c";
  const char* receiverVal = (score.firstServer == 0) ? "c" : "a";

  JsonDocument doc;
  doc["starting_server"]   = serverVal;
  doc["starting_receiver"] = receiverVal;
  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  http.setTimeout(5000);
  http.setReuse(false);
  if (!http.begin(*_getClient(), url)) { _resetClient(); return false; }
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(payload);
  http.end();

  if (code < 0) { _resetClient(); return false; }
  Serial.printf("[Firebase] writeFirstServer OK: server=%s receiver=%s (code %d)\n",
    serverVal, receiverVal, code);
  return code == 200;
}

// ── Write hardcap ─────────────────────────────────────────────────────────────

inline bool writeHardcap(uint8_t hardcap) {
  String channel = getChannel();
  if (channel.isEmpty()) return false;
  if (!WiFi.isConnected()) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) return false;

  HTTPClient http;
  http.setTimeout(5000);
  http.setReuse(false);
  String url = String(FIREBASE_DATABASE_URL)
    + "/match-" + channel + "/game_settings/hardcap.json";
  if (!http.begin(*_getClient(), url)) { _resetClient(); return false; }
  http.addHeader("Content-Type", "application/json");
  int code = http.PUT(String(hardcap));
  http.end();
  if (code < 0) { _resetClient(); return false; }
  Serial.printf("[Firebase] writeHardcap OK: %d (code %d)\n", hardcap, code);
  return code == 200;
}

// ── Write win_points directly to its leaf node (avoids overwriting siblings) ──

inline bool writeWinPoints(uint8_t winPoints) {
  String channel = getChannel();
  if (channel.isEmpty()) return false;
  if (!WiFi.isConnected()) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) return false;

  HTTPClient http;
  http.setTimeout(5000);
  http.setReuse(false);

  String url = String(FIREBASE_DATABASE_URL)
    + "/match-" + channel + "/game_settings/win_points.json";

  if (!http.begin(*_getClient(), url)) { _resetClient(); return false; }
  http.addHeader("Content-Type", "application/json");
  int code = http.PUT(String(winPoints));
  http.end();

  if (code < 0) { _resetClient(); return false; }
  Serial.printf("[Firebase] writeWinPoints OK: %d (code %d)\n", winPoints, code);
  return code == 200;
}

// ── Write format (as game_settings.set_mode, the shared "Game Mode" field) ──
// Written as a plain digit string ("1"/"2"/"3" = total sets in the match) —
// the same convention the Fwango bridge already writes via numberOfGames —
// so every existing reader (friend's tool, TournamentLiveScores) parses it
// correctly without needing to special-case this board as a writer.

inline bool writeFormat(uint8_t format) {
  String channel = getChannel();
  if (channel.isEmpty()) return false;
  if (!WiFi.isConnected()) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) return false;

  const char* setMode = (format == 0) ? "1" : (format == 1) ? "2" : "3";

  HTTPClient http;
  http.setTimeout(5000);
  http.setReuse(false);
  String url = String(FIREBASE_DATABASE_URL)
    + "/match-" + channel + "/game_settings/set_mode.json";
  if (!http.begin(*_getClient(), url)) { _resetClient(); return false; }
  http.addHeader("Content-Type", "application/json");
  int code = http.PUT("\"" + String(setMode) + "\"");
  http.end();

  if (code < 0) { _resetClient(); return false; }
  Serial.printf("[Firebase] writeFormat OK: %s (code %d)\n", setMode, code);
  return code == 200;
}

// ── Shared timer (timeout/medical/break) state ───────────────────────────────
// Mirrors the RTDB "match-<channel>/timer" node also written by the manager
// server's firebaseBridge.js for CENTRAL-mode boards (see that file's
// pushUpToFirebase/pushDownToBoard) and read/written by scoreboard/'s
// input.html + index.html. No duration field on purpose: every reader keeps
// its own copy of the same fixed durations this board already hardcodes in
// score_actions.h (BREAK/TIMEOUT/MEDICAL_*_MS) — RTDB only ever carries
// *which* timer (if any) is running, never how long it lasts.
// Timers are passed around as timer keys ("timeout", "a/timeout",
// "b/medical", "break" — same strings as ScoreActions::activeTimerKey() and
// its apply() commands): the "a/"/"b/" prefix maps to the node's optional
// `team: "a"|"b"`, the rest to `type`.

inline bool writeTimerState(const char* key) {
  const char* type = key;
  const char* team = nullptr;
  if (key && (key[0] == 'a' || key[0] == 'b') && key[1] == '/') {
    team = (key[0] == 'a') ? "a" : "b";
    type = key + 2;
  }
  String channel = getChannel();
  if (channel.isEmpty()) return false;
  if (!WiFi.isConnected()) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) return false;

  HTTPClient http;
  http.setTimeout(5000);
  http.setReuse(false);
  String url = String(FIREBASE_DATABASE_URL) + "/match-" + channel + "/timer.json";
  if (!http.begin(*_getClient(), url)) { _resetClient(); return false; }
  http.addHeader("Content-Type", "application/json");

  int code;
  if (type == nullptr || type[0] == '\0') {
    code = http.PUT("null"); // deletes the whole "timer" node
  } else {
    JsonDocument doc;
    doc["type"] = type;
    if (team) doc["team"] = team;
    doc["started_at"][".sv"] = "timestamp"; // Firebase server-timestamp sentinel
    String payload;
    serializeJson(doc, payload);
    // PUT (not PATCH): replaces the node wholesale so a stale started_at from
    // a previous timer of the same type never lingers under the new one.
    code = http.PUT(payload);
  }
  http.end();

  if (code < 0) { _resetClient(); return false; }
  Serial.printf("[Firebase] writeTimerState OK: %s (code %d)\n", (key && key[0]) ? key : "(none)", code);
  return code == 200;
}

// Returns true on a successful read, with outType set to the timer key
// ("timeout"/"medical"/"break", prefixed "a/"/"b/" when the node has a
// `team`), or "" when no timer is currently active in Firebase. outElapsedMs
// is how long that timer has already been running, computed from the node's
// "started_at" (a Firebase server timestamp) against this same response's
// HTTP "Date" header — both timestamps come from Firebase's own frontend, so
// no local wall-clock/NTP sync is needed. Set to -1 (unknown) when outType
// is "" or when either timestamp couldn't be read/parsed — callers that
// can't trust an elapsed time should treat that the same as "just don't
// know", not as "0 elapsed".
inline bool readTimerState(String& outType, long& outElapsedMs) {
  outElapsedMs = -1;
  String channel = getChannel();
  if (channel.isEmpty()) return false;
  if (!WiFi.isConnected()) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) return false;

  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);
  String url = String(FIREBASE_DATABASE_URL) + "/match-" + channel + "/timer.json";
  if (!http.begin(*_getClient(), url)) { _resetClient(); return false; }
  const char* headerKeys[] = {"Date"};
  http.collectHeaders(headerKeys, 1);
  int code = http.GET();
  if (code != 200) {
    http.end();
    if (code < 0) _resetClient();
    return false;
  }
  String payload = http.getString();
  String dateHeader = http.header("Date");
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err || doc.isNull()) {
    outType = ""; // "null" (no timer) or unexpected/unparseable shape
    return true;
  }
  outType = String((const char*)(doc["type"] | ""));
  String team = String((const char*)(doc["team"] | ""));
  if ((outType == "timeout" || outType == "medical") && (team == "a" || team == "b")) {
    outType = team + "/" + outType;
  }
  if (outType.length() > 0 && doc["started_at"].is<int64_t>()) {
    int64_t startedAt  = doc["started_at"];
    int64_t serverNow  = _parseHttpDateToEpochMs(dateHeader);
    int64_t elapsed    = serverNow - startedAt;
    // Cap at ~24 days so this always fits the 32-bit `long` outElapsedMs is
    // declared as — real elapsed times here are at most a few minutes
    // (BREAK/TIMEOUT/MEDICAL durations), this is just a sanity clamp against
    // a clock-skew/parse edge case producing a huge or negative delta.
    if (serverNow > 0 && startedAt > 0 && elapsed >= 0 && elapsed < 2000000000LL) {
      outElapsedMs = (long)elapsed;
    }
  }
  return true;
}

// ── Write team/player names ──────────────────────────────────────────────────
// PATCHes only match-<channel>/meta/team_a (or team_b), and only the
// name/player_1/player_2 leaves that are actually non-empty — meta also has
// other writers (roundnet-worlds-live's editor UI and Fwango bridge) and this
// must never overwrite color/country/phase/court/status, nor clobber a name
// set elsewhere with a blank one just because this board hasn't been given
// one yet.

inline bool _writeTeamSide(const String& channel, const char* side,
                            const char* name, const char* p1, const char* p2) {
  JsonDocument doc;
  bool any = false;
  if (name[0]) { doc["name"]     = name; any = true; }
  if (p1[0])   { doc["player_1"] = p1;   any = true; }
  if (p2[0])   { doc["player_2"] = p2;   any = true; }
  if (!any) return true; // nothing to push for this side — not an error

  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  http.setTimeout(5000);
  http.setReuse(false);
  String url = String(FIREBASE_DATABASE_URL) + "/match-" + channel + "/meta/" + side + ".json";
  if (!http.begin(*_getClient(), url)) { _resetClient(); return false; }
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(payload);
  http.end();

  if (code < 0) {
    Serial.printf("[Firebase] writeTeamNames(%s) network error: %d\n", side, code);
    _resetClient();
    return false;
  }
  if (code != 200) {
    Serial.printf("[Firebase] writeTeamNames(%s) FAILED, HTTP %d\n", side, code);
    return false;
  }
  Serial.printf("[Firebase] writeTeamNames(%s) OK\n", side);
  return true;
}

inline bool writeTeamNames(const TeamNames::Names& n) {
  String channel = getChannel();
  if (channel.isEmpty()) return false;
  if (!WiFi.isConnected()) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) return false;

  bool okA = _writeTeamSide(channel, "team_a", n.teamA, n.playerA1, n.playerA2);
  bool okB = _writeTeamSide(channel, "team_b", n.teamB, n.playerB1, n.playerB2);
  return okA && okB;
}

// ── Read team/player names ───────────────────────────────────────────────────
// Reads match-<channel>/teams_info (an official feed some tournaments already
// populate — not written by any code in this repo) and match-<channel>/meta
// (this project's own, editor-mode-writable fallback), and fills `out` with
// team_a/team_b's name/player_1/player_2 — preferring teams_info when a field
// is present there, exactly like roundnet-worlds-live's index.html already
// resolves display names (see deriveMatch()'s teamView()). Without this
// precedence, a channel with real tournament data in teams_info but nothing
// in meta (e.g. channel 6/7) would look empty to the board even though the
// live hub already shows names for it.

inline bool _fetchJson(const String& channel, const char* path, JsonDocument& doc) {
  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);
  String url = String(FIREBASE_DATABASE_URL) + "/match-" + channel + "/" + path + ".json";
  if (!http.begin(*_getClient(), url)) { _resetClient(); return false; }
  int code = http.GET();
  if (code != 200) {
    http.end();
    if (code < 0) _resetClient();
    return false;
  }
  String payload = http.getString();
  http.end();
  return deserializeJson(doc, payload) == DeserializationError::Ok;
}

inline String _pickTeamField(bool haveOfficial, JsonDocument& official,
                              bool haveMeta, JsonDocument& meta,
                              const char* side, const char* key) {
  if (haveOfficial) {
    const char* v = official[side][key] | (const char*)nullptr;
    if (v && v[0]) return String(v);
  }
  if (haveMeta) {
    const char* v = meta[side][key] | (const char*)nullptr;
    if (v && v[0]) return String(v);
  }
  return String("");
}

inline bool readTeamNames(TeamNames::Names& out) {
  String channel = getChannel();
  if (channel.isEmpty()) return false;
  if (!WiFi.isConnected()) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) return false;

  JsonDocument official, meta;
  bool haveOfficial = _fetchJson(channel, "teams_info", official);
  bool haveMeta     = _fetchJson(channel, "meta", meta);
  if (!haveOfficial && !haveMeta) return false;

  strlcpy(out.teamA,    _pickTeamField(haveOfficial, official, haveMeta, meta, "team_a", "name").c_str(),     sizeof(out.teamA));
  strlcpy(out.playerA1, _pickTeamField(haveOfficial, official, haveMeta, meta, "team_a", "player_1").c_str(), sizeof(out.playerA1));
  strlcpy(out.playerA2, _pickTeamField(haveOfficial, official, haveMeta, meta, "team_a", "player_2").c_str(), sizeof(out.playerA2));
  strlcpy(out.teamB,    _pickTeamField(haveOfficial, official, haveMeta, meta, "team_b", "name").c_str(),     sizeof(out.teamB));
  strlcpy(out.playerB1, _pickTeamField(haveOfficial, official, haveMeta, meta, "team_b", "player_1").c_str(), sizeof(out.playerB1));
  strlcpy(out.playerB2, _pickTeamField(haveOfficial, official, haveMeta, meta, "team_b", "player_2").c_str(), sizeof(out.playerB2));
  return true;
}

} // namespace Firebase