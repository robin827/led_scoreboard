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
static String _channelCache = "";
static bool _channelLoaded = false;

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

  // Client SSL statique réutilisé entre les appels
  static WiFiClientSecure* client = nullptr;
  if (client == nullptr) {
    client = new WiFiClientSecure();
    client->setInsecure();
    client->setTimeout(10);  // 10s timeout
  }
  
  HTTPClient http;
  http.setTimeout(10000);  // 10s timeout
  http.setReuse(false);    // Désactive keep-alive

  // Lit TOUT le match en une seule requête
  String url = String(FIREBASE_DATABASE_URL)
    + "/match-" + channel + ".json?shallow=false";
  
  bool success = http.begin(*client, url);
  if (!success) {
    Serial.println("[Firebase] Failed to begin HTTP (DNS error?)");
    // Recrée le client sur échec
    delete client;
    client = nullptr;
    return false;
  }
  
  int code = http.GET();
  
  if (code != 200) {
    Serial.printf("[Firebase] Failed to read match: %d\n", code);
    http.end();
    
    // Si échec (code négatif = erreur réseau/SSL/DNS), recrée le client
    if (code < 0) {
      Serial.println("[Firebase] Network/SSL/DNS error, recreating client");
      delete client;
      client = nullptr;
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

} // namespace Firebase