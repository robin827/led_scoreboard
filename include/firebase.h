/**
 * firebase.h - Lecture Firebase Realtime Database
 */

#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include "config.h"
#include "score.h"

namespace Firebase {

static Preferences _prefs;

// ── Channel (match ID) ────────────────────────────────────────────────────

inline void setChannel(const String& matchId) {
  _prefs.begin("firebase", false);
  _prefs.putString("channel", matchId);
  _prefs.end();
  Serial.printf("[Firebase] Channel set: %s\n", matchId.c_str());
}

inline String getChannel() {
  _prefs.begin("firebase", true);
  String channel = _prefs.getString("channel", "");
  _prefs.end();
  return channel;
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

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  // Lit active_set
  String urlActiveSet = String(FIREBASE_DATABASE_URL)
    + "/match-" + channel + "/active_set.json";
  
  Serial.printf("[Firebase] Reading channel: match-%s\n", channel.c_str());
  Serial.printf("[Firebase] URL: %s\n", urlActiveSet.c_str());
  
  http.begin(client, urlActiveSet);
  int code = http.GET();
  
  if (code != 200) {
    Serial.printf("[Firebase] Failed to read active_set: %d\n", code);
    String response = http.getString();
    Serial.printf("[Firebase] Response: %s\n", response.c_str());
    http.end();
    return false;
  }

  String activeSetStr = http.getString();
  Serial.printf("[Firebase] active_set raw: %s\n", activeSetStr.c_str());
  int activeSet = activeSetStr.toInt();
  http.end();

  if (activeSet < 1) {
    Serial.println("[Firebase] Invalid active_set");
    return false;
  }

  // Lit le score du set actif
  String urlSet = String(FIREBASE_DATABASE_URL)
    + "/match-" + channel + "/score/set_" + String(activeSet) + ".json";
  
  http.begin(client, urlSet);
  code = http.GET();
  
  if (code != 200) {
    Serial.printf("[Firebase] Failed to read set_%d: %d\n", activeSet, code);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  // Parse JSON simple
  int idxA = payload.indexOf("\"team_a_score\":");
  int idxB = payload.indexOf("\"team_b_score\":");

  if (idxA < 0 || idxB < 0) {
    Serial.println("[Firebase] Invalid JSON");
    Serial.printf("[Firebase] Payload: %s\n", payload.c_str());
    return false;
  }

  score.scoreA = payload.substring(idxA + 15).toInt();
  score.scoreB = payload.substring(idxB + 15).toInt();

  // Compte les sets gagnés (simplifié : parcourt set_1 à set_{activeSet-1})
  score.setA = 0;
  score.setB = 0;

  for (int i = 1; i < activeSet; i++) {
    String urlPrevSet = String(FIREBASE_DATABASE_URL)
      + "/match-" + channel + "/score/set_" + String(i) + ".json";
    
    http.begin(client, urlPrevSet);
    if (http.GET() == 200) {
      String prevPayload = http.getString();
      int prevIdxA = prevPayload.indexOf("\"team_a_score\":");
      int prevIdxB = prevPayload.indexOf("\"team_b_score\":");
      
      if (prevIdxA >= 0 && prevIdxB >= 0) {
        int prevScoreA = prevPayload.substring(prevIdxA + 15).toInt();
        int prevScoreB = prevPayload.substring(prevIdxB + 15).toInt();
        
        // Règle roundnet : ≥21 avec écart de 2
        if (prevScoreA >= 21 && (prevScoreA - prevScoreB) >= 2) {
          score.setA++;
        } else if (prevScoreB >= 21 && (prevScoreB - prevScoreA) >= 2) {
          score.setB++;
        }
      }
    }
    http.end();
  }

  Serial.printf("[Firebase] Read OK: A=%d B=%d (sets %d-%d)\n",
    score.scoreA, score.scoreB, score.setA, score.setB);

  return true;
}

} // namespace Firebase