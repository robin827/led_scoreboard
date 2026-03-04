/**
 * mode.h - Gestion des modes LOCAL / READ
 */

#pragma once
#include <Arduino.h>
#include <Preferences.h>

enum class AppMode : uint8_t {
  LOCAL = 0,  // Score local uniquement
  READ  = 1   // Lecture depuis Firebase
};

namespace Mode {

static AppMode _current = AppMode::LOCAL;
static Preferences _prefs;

inline void init() {
  _prefs.begin("mode", true);
  _current = (AppMode)_prefs.getUChar("mode", 0);
  _prefs.end();
  Serial.printf("[MODE] Current: %s\n", _current == AppMode::LOCAL ? "LOCAL" : "READ");
}

inline void set(AppMode mode) {
  _current = mode;
  _prefs.begin("mode", false);
  _prefs.putUChar("mode", (uint8_t)mode);
  _prefs.end();
  Serial.printf("[MODE] Changed to: %s\n", mode == AppMode::LOCAL ? "LOCAL" : "READ");
}

inline AppMode get() {
  return _current;
}

inline bool isLocal() {
  return _current == AppMode::LOCAL;
}

inline bool isRead() {
  return _current == AppMode::READ;
}

} // namespace Mode
