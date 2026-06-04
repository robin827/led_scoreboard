/**
 * mode.h - LOCAL / SYNC modes
 */

#pragma once
#include <Arduino.h>
#include <Preferences.h>

enum class AppMode : uint8_t {
  LOCAL = 0,  // standalone, no cloud
  SYNC  = 1   // bidirectional Firebase sync + local commands
};

namespace Mode {

static AppMode _current = AppMode::LOCAL;
static Preferences _prefs;

inline void init() {
  _prefs.begin("mode", false);
  uint8_t raw = _prefs.getUChar("mode", 0);
  // Remap legacy READ(1)/WRITE(2) → SYNC(1)
  if (raw > 1) raw = 1;
  _current = (AppMode)raw;
  _prefs.end();
  Serial.printf("[MODE] Current: %s\n", _current == AppMode::SYNC ? "SYNC" : "LOCAL");
}

inline void set(AppMode mode) {
  _current = mode;
  _prefs.begin("mode", false);
  _prefs.putUChar("mode", (uint8_t)mode);
  _prefs.end();
  Serial.printf("[MODE] Changed to: %s\n", mode == AppMode::SYNC ? "SYNC" : "LOCAL");
}

inline AppMode get()   { return _current; }
inline bool isLocal()  { return _current == AppMode::LOCAL; }
inline bool isSync()   { return _current == AppMode::SYNC; }

} // namespace Mode
