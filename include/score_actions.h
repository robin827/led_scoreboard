#pragma once
#include <string.h>
#include "score.h"
#include "led.h"
#include "mode.h"

extern Score currentScore;
extern SemaphoreHandle_t scoreMutex;

namespace ScoreActions {

// ESP-NOW commands: a/single, a/double, a/long, b/single, b/double, b/long, reset
// Portal legacy commands also accepted: a/inc, a/dec, b/inc, b/dec, nextset, reset
// Returns true if the command was applied, false if read-only mode or nextset with tied score.
inline bool apply(const char* cmd) {
  if (Mode::isRead()) return false;

  xSemaphoreTake(scoreMutex, portMAX_DELAY);
  bool changed = true;
  bool ok      = true;

  const bool at00 = (currentScore.scoreA == 0 && currentScore.scoreB == 0);

  if      (strcmp(cmd, "a/single") == 0) currentScore.incrementA();
  else if (strcmp(cmd, "a/double") == 0) currentScore.decrementA();
  else if (strcmp(cmd, "b/single") == 0) currentScore.incrementB();
  else if (strcmp(cmd, "b/double") == 0) currentScore.decrementB();
  else if (strcmp(cmd, "a/long")   == 0) {
    if (at00) currentScore.firstServer = 0;       // Team A serves first
    else { ok = currentScore.nextSet(); changed = ok; }
  }
  else if (strcmp(cmd, "b/long")   == 0) {
    if (at00) currentScore.firstServer = 1;       // Team B serves first
    else { ok = currentScore.nextSet(); changed = ok; }
  }
  // Legacy portal commands
  else if (strcmp(cmd, "a/inc")   == 0) currentScore.incrementA();
  else if (strcmp(cmd, "a/dec")   == 0) currentScore.decrementA();
  else if (strcmp(cmd, "b/inc")   == 0) currentScore.incrementB();
  else if (strcmp(cmd, "b/dec")   == 0) currentScore.decrementB();
  else if (strcmp(cmd, "nextset") == 0) { ok = currentScore.nextSet(); changed = ok; }
  else if (strcmp(cmd, "reset")   == 0) currentScore.reset();
  else { changed = false; ok = false; }

  if (changed) LED::update(currentScore);
  xSemaphoreGive(scoreMutex);
  return ok;
}

} // namespace ScoreActions
