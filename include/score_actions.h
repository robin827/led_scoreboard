#pragma once
#include <string.h>
#include <Preferences.h>
#include "score.h"
#include "led.h"
#include "mode.h"

extern Score currentScore;
extern SemaphoreHandle_t scoreMutex;

namespace ScoreActions {

static uint32_t _rotationCount   = 0;
static bool     _rotationPending  = false;
static bool     _timerActive      = false;
static uint32_t _timerStartMs     = 0;
static bool     _timeoutActive    = false;
static uint32_t _timeoutStartMs   = 0;

// Battery saver
static uint32_t        _lastActivityMs      = 0;
static uint16_t        _dimTimeoutSec       = 300;
static bool            _dimActive           = false;
static volatile bool   _wantsWake           = false;
static uint8_t         _preSleepBrightness  = 100;

static constexpr uint32_t BREAK_DURATION_MS    = 3UL * 60UL * 1000UL;
static constexpr uint32_t TIMEOUT_COUNTDOWN_MS = 60UL * 1000UL;

inline void initBatterySaver() {
  Preferences prefs;
  prefs.begin("batsaver", true);
  _dimTimeoutSec = prefs.getUShort("timeoutSec", 300);
  prefs.end();
  _lastActivityMs = millis();
}

inline uint16_t getDimTimeoutSec()       { return _dimTimeoutSec; }
inline uint8_t  getPreSleepBrightness()  { return _preSleepBrightness; }

inline void activateSleep() {
  _preSleepBrightness = LED::getBrightness();
  _dimActive = true;
}

inline void setDimTimeout(uint16_t secs) {
  _dimTimeoutSec = secs;
  Preferences prefs;
  prefs.begin("batsaver", false);
  prefs.putUShort("timeoutSec", secs);
  prefs.end();
}

// Call from any user interaction (safe from any core — no LED calls here)
inline void notifyActivity() {
  _lastActivityMs = millis();
  if (_dimActive) _wantsWake = true;
}

inline bool isDimActive() { return _dimActive; }

// Call from main loop (Core 1) only — all LED changes happen here
inline void tickBatterySaver() {
  if (_wantsWake && _dimActive) {
    _wantsWake = false;
    _dimActive = false;
    xSemaphoreTake(scoreMutex, portMAX_DELAY);
    LED::update(currentScore);
    xSemaphoreGive(scoreMutex);
    return;
  }
  if (_dimTimeoutSec == 0 || _dimActive) return;
  uint32_t timeoutMs = (uint32_t)_dimTimeoutSec * 1000UL;
  if (millis() - _lastActivityMs >= timeoutMs) {
    _preSleepBrightness = LED::getBrightness();
    _dimActive = true;
  }
}

inline bool     getAndClearRotation() { bool r = _rotationPending; _rotationPending = false; return r; }
inline uint32_t getRotationCount()    { return _rotationCount; }

inline bool isBreakTimerActive() { return _timerActive; }
inline uint32_t breakTimerRemainingMs() {
  if (!_timerActive) return 0;
  uint32_t elapsed = millis() - _timerStartMs;
  if (elapsed >= BREAK_DURATION_MS) { _timerActive = false; return 0; }
  return BREAK_DURATION_MS - elapsed;
}

inline bool isTimeoutActive() { return _timeoutActive; }
inline uint32_t timeoutCountdownMs() {
  if (!_timeoutActive) return 0;
  uint32_t elapsed = millis() - _timeoutStartMs;
  if (elapsed >= TIMEOUT_COUNTDOWN_MS) { _timeoutActive = false; return 0; }
  return TIMEOUT_COUNTDOWN_MS - elapsed;
}

// Apply a score received from Firebase (external change already detected by caller).
// Checks for rotation boundary exactly as apply() does for local increments.
inline void applyFromDatabase(const Score& db) {
  notifyActivity();
  xSemaphoreTake(scoreMutex, portMAX_DELAY);

  int oldTotal = currentScore.scoreA + currentScore.scoreB;
  int newTotal = db.scoreA + db.scoreB;
  bool sameSet = (db.setA == currentScore.setA && db.setB == currentScore.setB);

  currentScore = db;
  LED::update(currentScore);

  if (sameSet && newTotal > oldTotal && newTotal % 4 == 3) {
    _rotationCount++;
    _rotationPending = true;
  }

  xSemaphoreGive(scoreMutex);
}

inline bool apply(const char* cmd) {
  notifyActivity();
  xSemaphoreTake(scoreMutex, portMAX_DELAY);

  // Timeout command always wins: cancel break timer and start 60 s countdown
  if (strcmp(cmd, "timeout") == 0) {
    _timerActive    = false;
    _timeoutActive  = true;
    _timeoutStartMs = millis();
    xSemaphoreGive(scoreMutex);
    return true;
  }

  // Any other command cancels break timer without scoring
  if (_timerActive) {
    _timerActive = false;
    LED::update(currentScore);
    xSemaphoreGive(scoreMutex);
    return true;
  }

  // Any other command cancels timeout without scoring
  if (_timeoutActive) {
    _timeoutActive = false;
    LED::update(currentScore);
    xSemaphoreGive(scoreMutex);
    return true;
  }

  bool changed = true;
  bool ok      = true;
  const bool at00 = (currentScore.scoreA == 0 && currentScore.scoreB == 0);
  bool didIncrement = false;
  bool didNextSet   = false;

  if      (strcmp(cmd, "a/single") == 0 || strcmp(cmd, "a/inc") == 0) { currentScore.incrementA(); didIncrement = true; }
  else if (strcmp(cmd, "a/double") == 0 || strcmp(cmd, "a/dec") == 0) { currentScore.decrementA(); }
  else if (strcmp(cmd, "b/single") == 0 || strcmp(cmd, "b/inc") == 0) { currentScore.incrementB(); didIncrement = true; }
  else if (strcmp(cmd, "b/double") == 0 || strcmp(cmd, "b/dec") == 0) { currentScore.decrementB(); }
  else if (strcmp(cmd, "a/long")   == 0) {
    if (at00) currentScore.firstServer = 0;
    else { ok = currentScore.nextSet(); changed = ok; if (ok) didNextSet = true; }
  }
  else if (strcmp(cmd, "b/long")   == 0) {
    if (at00) currentScore.firstServer = 1;
    else { ok = currentScore.nextSet(); changed = ok; if (ok) didNextSet = true; }
  }
  else if (strcmp(cmd, "nextset") == 0) { ok = currentScore.nextSet(); changed = ok; if (ok) didNextSet = true; }
  else if (strcmp(cmd, "reset")   == 0) { currentScore.reset(); }
  else { changed = false; ok = false; }

  if (didIncrement) {
    if ((currentScore.scoreA + currentScore.scoreB) % 4 == 3) {
      _rotationCount++;
      _rotationPending = true;
    }
  }
  if (didNextSet) {
    _timerActive  = true;
    _timerStartMs = millis();
  }

  if (changed) LED::update(currentScore);
  xSemaphoreGive(scoreMutex);
  return ok;
}

} // namespace ScoreActions
