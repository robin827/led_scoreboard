/**
 * event_history.h - FIREBASE mode: log this board's own changes into
 * match-<channel>/event_history, the point-by-point log the friend's
 * scoreboard tool (scoreboard/js/Scoreboard.js) keeps for its score history
 * strip, chart and match statistics — in exactly the shape that tool writes:
 *   {type: "score", team: "a"|"b", score: N, set: N}   one per point
 *     (a decrease removes the score events above the new value instead)
 *   {type: "set", set: N}                               active set changed
 *   [{type: "reset"}]                                   reset replaces the whole log
 *   {type: "timeout"|"medical", team, set, score_a, score_b, started_at}
 *     team timer started; popped again if cancelled before any point was played
 * Capped at the last 200 entries, like the tool's handleEventHistory().
 *
 * Same logic as the manager server's eventHistory.js (CENTRAL mode) — keep
 * the two in step. Read-modify-write of the whole array (GET + PUT), like
 * the tool's own update() of the event_history path.
 */

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <algorithm>
#include "score.h"
#include "firebase.h"
#include "score_actions.h"

namespace EventHistory {

static constexpr size_t   MAX_EVENTS      = 200;
// A team timer that disappears with more than this much time left was
// cancelled (pop its event if no point was played since); closer to its full
// duration it ran out naturally (event stays).
static constexpr uint32_t CANCEL_GRACE_MS = 2000;

// The tool (and other writers) sometimes store numbers as strings.
inline int64_t _num(JsonVariantConst v) {
  if (v.is<const char*>()) return atoll(v.as<const char*>());
  return v.as<int64_t>();
}

inline bool _isScoreEvent(JsonObjectConst e, const char* team, int set) {
  return strcmp(e["type"] | "", "score") == 0 &&
         strcmp(e["team"] | "", team) == 0 &&
         _num(e["set"]) == set;
}

inline int _lastScoreFor(JsonArray h, const char* team, int set) {
  for (int i = (int)h.size() - 1; i >= 0; i--) {
    JsonObjectConst e = h[i];
    if (_isScoreEvent(e, team, set)) return (int)_num(e["score"]);
  }
  return 0;
}

inline void _logScoreChange(JsonArray h, const char* team, int set, int from, int to) {
  if (to == from) return;
  if (to < from) {
    // The tool's removeEventHistoryEvents(), repeated until every point above
    // the new score is gone (one sync can carry more than one decrement).
    for (int i = (int)h.size() - 1; i >= 0; i--) {
      JsonObjectConst e = h[i];
      if (_isScoreEvent(e, team, set) && _num(e["score"]) > to) h.remove(i);
    }
    return;
  }
  // One event per point, like the tool's +1 buttons — never re-log a point
  // the history already has.
  int logged = _lastScoreFor(h, team, set);
  int start  = (logged > from && logged <= to) ? logged : from;
  for (int s = start + 1; s <= to; s++) {
    JsonObject e = h.add<JsonObject>();
    e["type"]  = "score";
    e["team"]  = team;
    e["score"] = s;
    e["set"]   = set;
  }
}

// Timer key ("a/timeout", "b/medical") -> team timer; "", "break" and a bare
// "timeout"/"medical" (no team) aren't logged, same as the tool.
struct TeamTimer { bool valid = false; String type; String team; };
inline TeamTimer _teamTimer(const String& key) {
  TeamTimer t;
  if (key.length() > 2 && (key[0] == 'a' || key[0] == 'b') && key[1] == '/') {
    String type = key.substring(2);
    if (type == "timeout" || type == "medical") {
      t.valid = true;
      t.type  = type;
      t.team  = key.substring(0, 1);
    }
  }
  return t;
}

inline bool _lastEventIsTimer(JsonArray h, const TeamTimer& t) {
  if (h.size() == 0) return false;
  JsonObjectConst last = h[h.size() - 1];
  return t.type == (last["type"] | "") && t.team == (last["team"] | "");
}

// RTDB hands back an array, an array with null holes, or (sparse keys) an
// object — the tool only accepts a real array, so always rebuild one.
inline void _normalize(JsonVariantConst src, JsonArray out) {
  if (src.is<JsonArrayConst>()) {
    for (JsonVariantConst v : src.as<JsonArrayConst>()) {
      if (v.is<JsonObjectConst>()) out.add(v);
    }
  } else if (src.is<JsonObjectConst>()) {
    std::vector<std::pair<long, JsonVariantConst>> items;
    for (JsonPairConst kv : src.as<JsonObjectConst>()) {
      if (kv.value().is<JsonObjectConst>()) items.push_back({atol(kv.key().c_str()), kv.value()});
    }
    std::sort(items.begin(), items.end(),
              [](const std::pair<long, JsonVariantConst>& a, const std::pair<long, JsonVariantConst>& b) { return a.first < b.first; });
    for (auto& it : items) out.add(it.second);
  }
}

// GET match-<channel>/event_history, plus Firebase's own clock (HTTP Date
// header, 0 if unparseable) for the cancelled-vs-ran-out check.
inline bool _read(JsonDocument& doc, int64_t& serverNowMs) {
  serverNowMs = 0;
  String channel = Firebase::getChannel();
  if (channel.isEmpty() || !WiFi.isConnected() || WiFi.localIP()[0] == 0) return false;

  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);
  String url = Firebase::_url(channel, "/event_history");
  if (!http.begin(*Firebase::_getClient(), url)) { Firebase::_resetClient(); return false; }
  const char* headerKeys[] = {"Date"};
  http.collectHeaders(headerKeys, 1);
  int code = http.GET();
  if (code != 200) {
    http.end();
    if (code < 0) Firebase::_resetClient();
    Serial.printf("[Firebase] event_history read FAILED: %d\n", code);
    return false;
  }
  String payload = http.getString();
  serverNowMs = Firebase::_parseHttpDateToEpochMs(http.header("Date"));
  http.end();
  if (deserializeJson(doc, payload) != DeserializationError::Ok) doc.clear(); // "null" -> empty
  return true;
}

inline bool _write(JsonDocument& doc) {
  String channel = Firebase::getChannel();
  if (channel.isEmpty() || !WiFi.isConnected() || WiFi.localIP()[0] == 0) return false;

  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);
  String url = Firebase::_url(channel, "/event_history");
  if (!http.begin(*Firebase::_getClient(), url)) { Firebase::_resetClient(); return false; }
  http.addHeader("Content-Type", "application/json");
  int code = http.PUT(payload);
  http.end();
  Firebase::_checkAuthCode(code);
  if (code < 0) { Firebase::_resetClient(); return false; }
  Serial.printf("[Firebase] event_history PUT (%u entries, code %d)\n", (unsigned)doc.size(), code);
  return code == 200;
}

// prev/prevTimer: what Firebase held before this board's change (main.cpp's
// lastLogged*); next/nextTimer: the board's new state. Timer strings are
// timer keys (ScoreActions::activeTimerKey()). Returns true when the history
// is up to date (written, or nothing to log), false on a network error.
inline bool sync(const Score& prevIn, const String& prevTimerKey,
                 const Score& next, const String& nextTimerKey, bool reset) {
  JsonDocument raw;
  int64_t serverNow = 0;
  if (!_read(raw, serverNow)) return false;

  JsonDocument doc;
  JsonArray h = doc.to<JsonArray>();
  _normalize(raw.as<JsonVariantConst>(), h);
  raw.clear();
  String before;
  serializeJson(doc, before);

  Score prev = prevIn;
  TeamTimer prevTimer = _teamTimer(prevTimerKey);
  TeamTimer nextTimer = _teamTimer(nextTimerKey);

  if (reset) {
    doc.clear();
    h = doc.to<JsonArray>();
    h.add<JsonObject>()["type"] = "reset";
    prev = Score{};
    prevTimer = TeamTimer{};
  }

  // Timer ended without a new one starting — cancelled or ran out. Before the
  // score pass, so a point played after a cancel doesn't hide the cancel.
  if (prevTimer.valid && nextTimerKey.length() == 0 && _lastEventIsTimer(h, prevTimer)) {
    int64_t startedAt = _num(h[h.size() - 1]["started_at"]);
    uint32_t duration = prevTimer.type == "timeout" ? ScoreActions::TIMEOUT_COUNTDOWN_MS
                                                    : ScoreActions::MEDICAL_DURATION_MS;
    if (startedAt <= 0) {
      h.remove(h.size() - 1);
    } else if (serverNow > 0 && (int64_t)duration - (serverNow - startedAt) > (int64_t)CANCEL_GRACE_MS) {
      h.remove(h.size() - 1);
    }
  }

  int prevSet = prev.setA + prev.setB + 1;
  int nextSet = next.setA + next.setB + 1;
  if (nextSet == prevSet) {
    _logScoreChange(h, "a", nextSet, prev.scoreA, next.scoreA);
    _logScoreChange(h, "b", nextSet, prev.scoreB, next.scoreB);
  } else {
    if (nextSet > prevSet) {
      // The set that just ended: catch up to its final score if the deciding
      // point(s) arrived in the same sync as the set change.
      int i = prevSet - 1;
      int finalA = i < 3 ? next.histA[i] : prev.scoreA;
      int finalB = i < 3 ? next.histB[i] : prev.scoreB;
      _logScoreChange(h, "a", prevSet, prev.scoreA, finalA);
      _logScoreChange(h, "b", prevSet, prev.scoreB, finalB);
      for (int s = prevSet + 1; s <= nextSet; s++) {
        JsonObject e = h.add<JsonObject>();
        e["type"] = "set";
        e["set"]  = s;
      }
    } else {
      JsonObject e = h.add<JsonObject>();
      e["type"] = "set";
      e["set"]  = nextSet;
    }
    _logScoreChange(h, "a", nextSet, 0, next.scoreA);
    _logScoreChange(h, "b", nextSet, 0, next.scoreB);
  }

  // New team timer (or switched team/type) — the tool's startTimer(): a
  // switch before any point was played replaces the previous event.
  if (nextTimer.valid &&
      (!prevTimer.valid || prevTimer.type != nextTimer.type || prevTimer.team != nextTimer.team)) {
    if (prevTimer.valid && _lastEventIsTimer(h, prevTimer)) h.remove(h.size() - 1);
    JsonObject e = h.add<JsonObject>();
    e["type"]    = nextTimer.type;
    e["team"]    = nextTimer.team;
    e["set"]     = nextSet;
    e["score_a"] = next.scoreA;
    e["score_b"] = next.scoreB;
    e["started_at"][".sv"] = "timestamp"; // Firebase server time (ms), the tool writes Date.now()
  }

  while (h.size() > MAX_EVENTS) h.remove(0);

  String after;
  serializeJson(doc, after);
  if (after == before) return true;
  return _write(doc);
}

} // namespace EventHistory
