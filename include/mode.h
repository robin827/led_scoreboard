/**
 * mode.h - Gestion des modes LOCAL / READ / WRITE
 */

#pragma once
#include <Arduino.h>
#include <Preferences.h>

enum class AppMode : uint8_t {
  LOCAL = 0,  // Score local uniquement
  READ  = 1,  // Lecture depuis Firebase
  WRITE = 2   // Ecriture vers Firebase (LOCAL + sync cloud)
};

namespace Mode {

static AppMode _current = AppMode::LOCAL;
static Preferences _prefs;

inline void init() {
  _prefs.begin("mode", true);
  _current = (AppMode)_prefs.getUChar("mode", 0);
  _prefs.end();
  const char* names[] = {"LOCAL", "READ", "WRITE"};
  Serial.printf("[MODE] Current: %s\n", names[(uint8_t)_current]);
}

inline void set(AppMode mode) {
  _current = mode;
  _prefs.begin("mode", false);
  _prefs.putUChar("mode", (uint8_t)mode);
  _prefs.end();
  const char* names[] = {"LOCAL", "READ", "WRITE"};
  Serial.printf("[MODE] Changed to: %s\n", names[(uint8_t)mode]);
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

inline bool isWrite() {
  return _current == AppMode::WRITE;
}

} // namespace Mode
