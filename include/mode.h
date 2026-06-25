/**
 * mode.h - LOCAL / SYNC modes
 */

#pragma once
#include <Arduino.h>
#include <Preferences.h>

enum class AppMode : uint8_t {
  LOCAL    = 0,  // standalone, no network features
  CENTRAL  = 1,  // WebSocket connection to central server
  FIREBASE = 2   // direct bidirectional Firebase sync
};

namespace Mode {

static AppMode _current  = AppMode::CENTRAL;
static AppMode _intended = AppMode::CENTRAL;  // user's choice, persisted to NVS
static Preferences _prefs;

inline const char* _modeName(AppMode m) {
  switch (m) {
    case AppMode::CENTRAL:  return "CENTRAL";
    case AppMode::FIREBASE: return "FIREBASE";
    default:                return "LOCAL";
  }
}

inline void init() {
  _prefs.begin("mode", false);
  uint8_t raw = _prefs.getUChar("mode", (uint8_t)AppMode::CENTRAL);
  if (raw > 2) raw = (uint8_t)AppMode::CENTRAL;
  _intended = (AppMode)raw;
  _current  = AppMode::LOCAL;  // always start LOCAL; Portal::tick() restores intended once WiFi is up
  _prefs.end();
  Serial.printf("[MODE] Intended: %s (starting LOCAL until WiFi connects)\n", _modeName(_intended));
}

// User-initiated change: persists to NVS and updates both effective and intended mode.
inline void set(AppMode mode) {
  _current = _intended = mode;
  _prefs.begin("mode", false);
  _prefs.putUChar("mode", (uint8_t)mode);
  _prefs.end();
  Serial.printf("[MODE] Changed to: %s\n", _modeName(mode));
}

// Temporary override (e.g. WiFi lost): changes effective mode without touching NVS or intended.
inline void setEffective(AppMode mode) {
  _current = mode;
  Serial.printf("[MODE] Effective: %s (intended: %s)\n", _modeName(mode), _modeName(_intended));
}

inline AppMode get()         { return _current; }
inline AppMode getIntended() { return _intended; }
inline bool isLocal()        { return _current == AppMode::LOCAL; }
inline bool isCentral()      { return _current == AppMode::CENTRAL; }
inline bool isFirebase()     { return _current == AppMode::FIREBASE; }

} // namespace Mode
