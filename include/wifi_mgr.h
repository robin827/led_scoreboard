/**
 * wifi_mgr.h - Gestion WiFi AP + STA, non-blocking state machine
 */

#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"
#include "mode.h"

namespace WiFiMgr {

enum class StaState : uint8_t { IDLE, CONNECTING, CONNECTED };

static StaState    _staState      = StaState::IDLE;
static uint32_t    _connectStart  = 0;
static uint32_t    _lastRetryTime = 0;
static uint8_t     _retryCount    = 0;
static Preferences _prefs;
static String      _cachedSsid   = "";
static String      _cachedPass   = "";
static bool        _credsCached  = false;

// ── Credentials NVS ───────────────────────────────────────────────────────

inline void saveCredentials(const String& ssid, const String& password) {
  _prefs.begin("wifi", false);
  _prefs.putString("ssid", ssid);
  _prefs.putString("pass", password);
  _prefs.end();
  _cachedSsid  = ssid;
  _cachedPass  = password;
  _credsCached = true;
  Serial.printf("[WiFi] Credentials saved: %s\n", ssid.c_str());
}

inline bool loadCredentials(String& ssid, String& password) {
  if (_credsCached) {
    ssid     = _cachedSsid;
    password = _cachedPass;
    return ssid.length() > 0;
  }
  _prefs.begin("wifi", true);
  bool hasSsid = _prefs.isKey("ssid");
  if (hasSsid) {
    _cachedSsid = _prefs.getString("ssid", "");
    _cachedPass = _prefs.getString("pass", "");
  }
  _prefs.end();
  _credsCached = true;
  ssid     = _cachedSsid;
  password = _cachedPass;
  return ssid.length() > 0;
}

inline void clearCredentials() {
  _prefs.begin("wifi", false);
  _prefs.clear();
  _prefs.end();
  _cachedSsid  = "";
  _cachedPass  = "";
  _credsCached = false;
  _retryCount = 0;
  if (_staState != StaState::IDLE) {
    WiFi.disconnect(false);
    _staState = StaState::IDLE;
  }
  Serial.println("[WiFi] Credentials cleared");
}

// ── Scan réseaux ──────────────────────────────────────────────────────────

inline String scanNetworks() {
  Serial.println("[WiFi] Scanning networks...");
  int n = WiFi.scanNetworks();

  String json = "[";
  for (int i = 0; i < n && i < 20; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  json += "]";

  WiFi.scanDelete();
  return json;
}

// ── Init ──────────────────────────────────────────────────────────────────

inline void init() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  delay(500);
  Serial.printf("[WiFi] AP started: %s | IP: %s\n",
    AP_SSID, WiFi.softAPIP().toString().c_str());

  // Kick off STA connection non-blocking — tick() manages the result
  String ssid, pass;
  if (loadCredentials(ssid, pass)) {
    Serial.printf("[WiFi] Starting STA connection to '%s'...\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
    _connectStart = millis();
    _staState = StaState::CONNECTING;
  }
}

// ── Tick — non-blocking state machine ────────────────────────────────────

inline void tick() {
  // Ensure AP stays up
  if (WiFi.getMode() != WIFI_AP_STA) {
    Serial.println("[WiFi] WARNING: Mode changed, forcing AP+STA");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
  }

  // ── CONNECTED ──
  if (_staState == StaState::CONNECTED) {
    if (WiFi.status() != WL_CONNECTED) {
      _staState = StaState::IDLE;
      _lastRetryTime = millis();
      Serial.println("[WiFi] STA disconnected");
    }
    return;
  }

  // ── CONNECTING ──
  if (_staState == StaState::CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      _retryCount = 0;
      _staState = StaState::CONNECTED;
      if (WiFi.softAPIP()[0] == 0) {
        WiFi.softAP(AP_SSID, AP_PASSWORD);
      }
      Serial.printf("[WiFi] STA connected — IP: %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("[WiFi] AP active — IP: %s\n", WiFi.softAPIP().toString().c_str());
    } else if ((millis() - _connectStart) >= WIFI_CONNECT_TIMEOUT_MS) {
      WiFi.disconnect(false);
      _retryCount++;
      _lastRetryTime = millis();
      _staState = StaState::IDLE;
      Serial.printf("[WiFi] STA timed out (attempt %d/%d)\n", _retryCount, WIFI_MAX_RETRIES);
    }
    return;
  }

  // ── IDLE: schedule next retry ──
  if (Mode::isLocal()) return;
  if (_retryCount >= WIFI_MAX_RETRIES) return;

  if ((millis() - _lastRetryTime) >= WIFI_RETRY_INTERVAL_MS) {
    String ssid, pass;
    if (!loadCredentials(ssid, pass)) return;
    Serial.printf("[WiFi] Connecting to '%s' (attempt %d/%d)...\n",
      ssid.c_str(), _retryCount + 1, WIFI_MAX_RETRIES);
    WiFi.begin(ssid.c_str(), pass.c_str());
    _connectStart = millis();
    _staState = StaState::CONNECTING;
  }
}

// ── Accesseurs ────────────────────────────────────────────────────────────

inline bool isOnline()    { return WiFi.status() == WL_CONNECTED; }
inline String localIP()   { return WiFi.localIP().toString(); }
inline String apIP()      { return WiFi.softAPIP().toString(); }
inline String getSSID()   { return WiFi.status() == WL_CONNECTED ? WiFi.SSID() : ""; }
inline int32_t getRSSI()  { return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0; }

inline void resetRetryCount() {
  _retryCount = 0;
  _lastRetryTime = 0;
  // Abort any in-progress connection so tick() starts fresh immediately
  if (_staState == StaState::CONNECTING) {
    WiFi.disconnect(false);
    _staState = StaState::IDLE;
  }
  Serial.println("[WiFi] Retry counter reset");
}

} // namespace WiFiMgr
