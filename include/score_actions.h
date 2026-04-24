#pragma once
#include <string.h>
#include "score.h"
#include "led.h"
#include "mode.h"

extern Score currentScore;
extern SemaphoreHandle_t scoreMutex;

namespace ScoreActions {

// Commands mirror the portal URL paths: "a/inc", "a/dec", "b/inc", "b/dec",
// "nextset", "reset".
// Returns true if the command was applied, false if read-only mode or teams tied
// (nextset only).
inline bool apply(const char* cmd) {
  if (Mode::isRead()) return false;

  xSemaphoreTake(scoreMutex, portMAX_DELAY);
  bool changed = true;
  bool ok      = true;

  if      (strcmp(cmd, "a/inc")   == 0) currentScore.incrementA();
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
