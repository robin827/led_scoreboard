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

static AppMode _current = AppMode::CENTRAL;
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
  _current = (AppMode)raw;
  _prefs.end();
  Serial.printf("[MODE] Current: %s\n", _modeName(_current));
}

inline void set(AppMode mode) {
  _current = mode;
  _prefs.begin("mode", false);
  _prefs.putUChar("mode", (uint8_t)mode);
  _prefs.end();
  Serial.printf("[MODE] Changed to: %s\n", _modeName(mode));
}

inline AppMode get()     { return _current; }
inline bool isLocal()    { return _current == AppMode::LOCAL; }
inline bool isCentral()  { return _current == AppMode::CENTRAL; }
inline bool isFirebase() { return _current == AppMode::FIREBASE; }

} // namespace Mode
