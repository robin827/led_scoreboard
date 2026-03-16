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

namespace Firebase {

static Preferences _prefs;
static String _channelCache = "";
static bool _channelLoaded = false;

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

inline void setChannel(const String& matchId) {
  _channelCache = matchId;
  _channelLoaded = true;
  _prefs.begin("firebase", false);
  _prefs.putString("channel", matchId);
  _prefs.end();
  Serial.printf("[Firebase] Channel set: %s\n", matchId.c_str());
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

  // Parse game_settings.win_points (may be string "21" or number 21)
  int idxWP = payload.indexOf("\"win_points\":");
  if (idxWP >= 0) {
    int valStart = idxWP + 13;
    while (valStart < (int)payload.length() && (payload[valStart] == ' ' || payload[valStart] == '"')) valStart++;
    int wp = payload.substring(valStart).toInt();
    if (wp >= 5 && wp <= 99) score.winPoints = wp;
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

  // Compte les sets gagnés
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
        
        if (prevScoreA >= 21 && (prevScoreA - prevScoreB) >= 2) {
          score.setA++;
        } else if (prevScoreB >= 21 && (prevScoreB - prevScoreA) >= 2) {
          score.setB++;
        }
      }
    }
  }

  Serial.printf("[Firebase] Read OK: A=%d B=%d (sets %d-%d)\n",
    score.scoreA, score.scoreB, score.setA, score.setB);

  return true;
}

// ── Ecriture du score vers Firebase ───────────────────────────────────────

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

// ── Write starting_server for the active set ──────────────────────────────────
// firstServer=0 (Team A) → "a",  firstServer=1 (Team B) → "c"

inline bool writeFirstServer(const Score& score) {
  String channel = getChannel();
  if (channel.isEmpty()) return false;
  if (!WiFi.isConnected()) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) return false;

  int activeSet = score.setA + score.setB + 1;
  String setPath = String(FIREBASE_DATABASE_URL)
    + "/match-" + channel + "/score/set_" + String(activeSet);
  String serverVal  = (score.firstServer == 0) ? "\"a\"" : "\"c\"";
  String receiverVal = (score.firstServer == 0) ? "\"c\"" : "\"a\"";

  // Write starting_server
  HTTPClient http;
  http.setTimeout(5000);
  http.setReuse(false);
  if (!http.begin(*_getClient(), setPath + "/starting_server.json")) { _resetClient(); return false; }
  http.addHeader("Content-Type", "application/json");
  int code = http.PUT(serverVal);
  http.end();
  if (code < 0) { _resetClient(); return false; }

  // Write starting_receiver
  if (!http.begin(*_getClient(), setPath + "/starting_receiver.json")) { _resetClient(); return false; }
  http.addHeader("Content-Type", "application/json");
  code = http.PUT(receiverVal);
  http.end();
  if (code < 0) { _resetClient(); return false; }

  Serial.printf("[Firebase] writeFirstServer OK: server=%s receiver=%s\n",
    serverVal.c_str(), receiverVal.c_str());
  return true;
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

} // namespace Firebase