/**
 * wifi.h - Gestion WiFi AP + STA avec retry limité
 */

#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"

namespace WiFiMgr {

static bool         _staConnected  = false;
static uint32_t     _lastRetryTime = 0;
static uint8_t      _retryCount    = 0;
static Preferences  _prefs;

// ── Credentials NVS ───────────────────────────────────────────────────────

inline void saveCredentials(const String& ssid, const String& password) {
  _prefs.begin("wifi", false);
  _prefs.putString("ssid", ssid);
  _prefs.putString("pass", password);
  _prefs.end();
  _retryCount = 0;  // Reset retry counter
  Serial.printf("[WiFi] Credentials saved: %s\n", ssid.c_str());
}

inline bool loadCredentials(String& ssid, String& password) {
  _prefs.begin("wifi", true);
  ssid     = _prefs.getString("ssid", "");
  password = _prefs.getString("pass", "");
  _prefs.end();
  return ssid.length() > 0;
}

inline void clearCredentials() {
  _prefs.begin("wifi", false);
  _prefs.clear();
  _prefs.end();
  _retryCount = 0;
  Serial.println("[WiFi] Credentials cleared");
}

// ── Scan réseaux ──────────────────────────────────────────────────────────

inline String scanNetworks() {
  Serial.println("[WiFi] Scanning networks...");
  int n = WiFi.scanNetworks();
  
  String json = "[";
  for (int i = 0; i < n && i < 20; i++) {  // Max 20 réseaux
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  json += "]";
  
  WiFi.scanDelete();
  return json;
}

// ── Connexion STA ─────────────────────────────────────────────────────────

inline bool connectSTA() {
  String ssid, pass;
  if (!loadCredentials(ssid, pass)) {
    Serial.println("[WiFi] No STA credentials");
    return false;
  }

  Serial.printf("[WiFi] Connecting to '%s' (attempt %d/%d)...\n", 
    ssid.c_str(), _retryCount + 1, WIFI_MAX_RETRIES);
  
  WiFi.begin(ssid.c_str(), pass.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    _staConnected = true;
    _retryCount = 0;  // Reset on success
    Serial.printf("[WiFi] STA connected — IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }

  WiFi.disconnect(false);  // Important pour ne pas perturber l'AP
  _staConnected = false;
  Serial.println("[WiFi] STA connection failed");
  return false;
}

// ── Init ──────────────────────────────────────────────────────────────────

inline void init() {
  WiFi.mode(WIFI_AP_STA);   // Dual mode : AP + STA
  
  // Lance le hotspot AP
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.printf("[WiFi] AP started: %s | IP: %s\n", 
    AP_SSID, WiFi.softAPIP().toString().c_str());
  
  // Tente connexion STA si credentials disponibles
  connectSTA();
}

// ── Tick — retry limité ───────────────────────────────────────────────────

inline void tick() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!_staConnected) {
      _staConnected = true;
      _retryCount = 0;
      Serial.println("[WiFi] STA reconnected");
    }
    return;
  }

  // STA déconnectée
  if (_staConnected) {
    _staConnected = false;
    Serial.println("[WiFi] STA disconnected");
  }

  // Arrête après MAX_RETRIES échecs
  if (_retryCount >= WIFI_MAX_RETRIES) {
    return;  // Mode AP only permanent jusqu'au prochain boot
  }

  // Retry avec intervalle
  uint32_t now = millis();
  if ((now - _lastRetryTime) >= WIFI_RETRY_INTERVAL_MS) {
    _lastRetryTime = now;
    _retryCount++;
    connectSTA();
  }
}

// ── Accesseurs ────────────────────────────────────────────────────────────

inline bool isOnline()  { return WiFi.status() == WL_CONNECTED; }
inline String localIP() { return WiFi.localIP().toString(); }
inline String apIP()    { return WiFi.softAPIP().toString(); }

inline void resetRetryCount() {
  _retryCount = 0;
  Serial.println("[WiFi] Retry counter reset");
}

} // namespace WiFiMgr
