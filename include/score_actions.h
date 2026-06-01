#pragma once
#include <string.h>
#include "score.h"
#include "led.h"
#include "mode.h"

extern Score currentScore;
extern SemaphoreHandle_t scoreMutex;

namespace ScoreActions {

static uint32_t _rotationCount   = 0;    // increments each time players should rotate
static bool     _rotationPending  = false; // triggers LED animation in loop()
static bool     _timerActive      = false;
static uint32_t _timerStartMs     = 0;
static constexpr uint32_t BREAK_DURATION_MS = 3UL * 60UL * 1000UL;

inline bool getAndClearRotation() {
  bool r = _rotationPending;
  _rotationPending = false;
  return r;
}

inline uint32_t getRotationCount() { return _rotationCount; }

inline bool isBreakTimerActive() { return _timerActive; }

inline uint32_t breakTimerRemainingMs() {
  if (!_timerActive) return 0;
  uint32_t elapsed = millis() - _timerStartMs;
  if (elapsed >= BREAK_DURATION_MS) { _timerActive = false; return 0; }
  return BREAK_DURATION_MS - elapsed;
}

// ESP-NOW commands: a/single, a/double, a/long, b/single, b/double, b/long, reset
// Portal legacy commands also accepted: a/inc, a/dec, b/inc, b/dec, nextset, reset
// Returns true if the command was applied, false if read-only mode or nextset with tied score.
inline bool apply(const char* cmd) {
  if (Mode::isRead()) return false;

  xSemaphoreTake(scoreMutex, portMAX_DELAY);

  // Any command during the break timer cancels it and resumes 0-0 without scoring
  if (_timerActive) {
    _timerActive = false;
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
