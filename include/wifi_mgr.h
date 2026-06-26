/**
 * wifi_mgr.h - WiFi AP + STA using ESP32 native events + constant retry.
 *
 * State detection is event-driven (no status polling).
 * Retries are managed in tick() with a fixed 25s interval.
 * LOCAL mode skips retries entirely.  No hard retry limit.
 */

#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <atomic>
#include "config.h"
#include "mode.h"

namespace WiFiMgr {

static Preferences _prefs;
static String      _cachedSsid    = "";
static String      _cachedPass    = "";
static String      _scoreboardId  = "";
static bool              _credsCached   = false;
static std::atomic<bool> _online        {false};
static bool              _connecting    = false;
static bool              _staEnabled    = false;
static uint32_t    _lastRetryTime = 0;

// ── Credentials NVS ───────────────────────────────────────────────────────

inline bool loadCredentials(String& ssid, String& password) {
  if (_credsCached) { ssid = _cachedSsid; password = _cachedPass; return ssid.length() > 0; }
  _prefs.begin("wifi", false);
  if (_prefs.isKey("ssid")) {
    _cachedSsid = _prefs.getString("ssid", "");
    _cachedPass = _prefs.getString("pass", "");
  }
  _prefs.end();
  _credsCached = true;
  ssid = _cachedSsid; password = _cachedPass;
  return ssid.length() > 0;
}

inline void saveCredentials(const String& ssid, const String& password) {
  _prefs.begin("wifi", false);
  _prefs.putString("ssid", ssid);
  _prefs.putString("pass", password);
  _prefs.end();
  _cachedSsid = ssid; _cachedPass = password; _credsCached = true;
  Serial.printf("[WiFi] Credentials saved: %s\n", ssid.c_str());
  _online = false; _connecting = true;
  WiFi.begin(ssid.c_str(), password.c_str());
}

inline void clearCredentials() {
  _prefs.begin("wifi", false);
  _prefs.clear();
  _prefs.end();
  _cachedSsid = ""; _cachedPass = ""; _credsCached = false;
  _online = false; _connecting = false;
  WiFi.disconnect(false);
  Serial.println("[WiFi] Credentials cleared");
}

// ── WiFi event handler ─────────────────────────────────────────────────────

static void _onEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      _online.store(true, std::memory_order_relaxed); _connecting = false;
      WiFi.setSleep(false);  // modem sleep disabled — ESP-NOW drops packets when radio is sleeping
      Serial.printf("[WiFi] Connected — STA: %s | AP: %s\n",
        WiFi.localIP().toString().c_str(), WiFi.softAPIP().toString().c_str());
      break;

    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      _online.store(false, std::memory_order_relaxed);
      _connecting = false;
      _lastRetryTime = millis();
      Serial.println("[WiFi] Disconnected — retry in 25s");
      break;

    default: break;
  }
}

// ── Scan networks ──────────────────────────────────────────────────────────

inline String scanNetworks() {
  if (_connecting) {
    Serial.println("[WiFi] Scan skipped: connection in progress");
    return "[]";
  }
  Serial.println("[WiFi] Scanning...");
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
  _prefs.begin("wifi", false);
  _scoreboardId = _prefs.getString("boardId", SCOREBOARD_ID);
  _staEnabled   = _prefs.getBool("staEn", false);
  _prefs.end();

  WiFi.onEvent(_onEvent);
  WiFi.persistent(false);         // credentials managed by NVS, not the SDK
  WiFi.setAutoReconnect(false);   // we manage retries ourselves via tick()
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(_scoreboardId.c_str(), AP_PASSWORD);
  delay(500);
  Serial.printf("[WiFi] AP started: %s | IP: %s | STA %s\n",
    _scoreboardId.c_str(), WiFi.softAPIP().toString().c_str(),
    _staEnabled ? "enabled" : "disabled");

  String ssid, pass;
  if (_staEnabled && loadCredentials(ssid, pass)) {
    Serial.printf("[WiFi] Connecting to '%s'...\n", ssid.c_str());
    _connecting = true;
    WiFi.begin(ssid.c_str(), pass.c_str());
  }
}

// ── Tick ──────────────────────────────────────────────────────────────────

inline void tick() {
  // Restore softAP if it dropped (can happen briefly after STA associates)
  if (WiFi.softAPIP()[0] == 0) {
    Serial.println("[WiFi] AP dropped, restoring...");
    WiFi.softAP(_scoreboardId.c_str(), AP_PASSWORD);
  }

  if (!_staEnabled || _online || _connecting) return;
  if (!_credsCached || _cachedSsid.length() == 0) return;
  if ((millis() - _lastRetryTime) < 25000u) return;

  Serial.printf("[WiFi] Retrying '%s'...\n", _cachedSsid.c_str());
  _connecting = true;
  WiFi.begin(_cachedSsid.c_str(), _cachedPass.c_str());
}

// ── Accessors ─────────────────────────────────────────────────────────────

inline bool     isOnline()        { return _online.load(std::memory_order_relaxed); }
inline bool     isConnecting()    { return _connecting; }
inline String   localIP()         { return WiFi.localIP().toString(); }
inline String   apIP()            { return WiFi.softAPIP().toString(); }
inline String   getSSID()         { return _online ? WiFi.SSID() : ""; }
inline int32_t  getRSSI()         { return _online ? WiFi.RSSI() : 0; }
inline String   getScoreboardId() { return _scoreboardId; }

inline void setScoreboardId(const String& id) {
  if (id.length() == 0 || id.length() > 31) return;
  _scoreboardId = id;
  _prefs.begin("wifi", false);
  _prefs.putString("boardId", id);
  _prefs.end();
  WiFi.softAP(_scoreboardId.c_str(), AP_PASSWORD);
  Serial.printf("[WiFi] Scoreboard ID updated: %s\n", _scoreboardId.c_str());
}

// Called after new credentials are submitted via the portal
inline void resetRetryCount() {
  _lastRetryTime = 0;
}

inline void enableSta() {
  _staEnabled = true;
  _prefs.begin("wifi", false);
  _prefs.putBool("staEn", true);
  _prefs.end();
  String ssid, pass;
  if (loadCredentials(ssid, pass)) {
    _connecting = true;
    WiFi.begin(ssid.c_str(), pass.c_str());
  }
  Serial.println("[WiFi] STA enabled");
}

inline void disableSta() {
  _staEnabled = false;
  _prefs.begin("wifi", false);
  _prefs.putBool("staEn", false);
  _prefs.end();
  _online = false;
  _connecting = false;
  WiFi.disconnect(false);
  Serial.println("[WiFi] STA disabled");
}

inline bool isStaEnabled() { return _staEnabled; }

} // namespace WiFiMgr
