/**
 * score_persist.h - Persists currentScore across reboots (NVS)
 *
 * Restores the last known game state (score, sets, win points, hardcap,
 * format, completed-set history) on boot, so a crash/power-loss/OTA reboot
 * doesn't silently drop back to 0-0 — most noticeable in LOCAL mode, where
 * there's no server/Firebase to resync from, but harmless in CENTRAL/
 * FIREBASE mode too (and arguably helpful there: a board that reboots mid-
 * match and comes back still holding the real score won't push a spurious
 * "0-0" up over its own real state on reconnect).
 *
 * Team/player names are already persisted separately (team_names.h, NVS
 * namespace "teams") — nothing to do there, only Score was missing this.
 *
 * Written as a single raw-bytes blob (not per-field putUChar calls, unlike
 * this codebase's other Preferences usage) specifically to minimize NVS
 * write wear: Score is a small POD struct (all uint8_t fields, no padding),
 * so one putBytes() per persist is a single flash log entry instead of one
 * per field. Persisted on a periodic, diffed tick (see tick()) rather than
 * on every mutation, for the same reason — a live match can see a scored
 * point every few seconds, across ~9 different call sites (portal buttons,
 * WS commands, ESP-NOW pedal, Firebase sync), and NVS wear-leveling is not
 * infinite. A tick-based diff also means every mutation site is covered
 * automatically, without needing a persistence hook at each one.
 */

#pragma once
#include <Preferences.h>
#include <string.h>
#include "score.h"

extern Score currentScore;
extern SemaphoreHandle_t scoreMutex;

namespace ScorePersist {

static constexpr uint32_t PERSIST_INTERVAL_MS = 2000;
static Score    _lastPersisted;
static uint32_t _lastPersistMs = 0;

// Writes the given snapshot to NVS unconditionally. Does NOT touch
// scoreMutex — every caller here already either holds it or has just
// released it, and score_actions.h's "reset" command (the other caller,
// via persistNowLocked() below) needs a variant that doesn't re-take a
// mutex it's still holding.
inline void _writeSnapshot(const Score& s) {
  Preferences prefs;
  prefs.begin("score", false);
  prefs.putBytes("state", &s, sizeof(Score));
  prefs.end();

  _lastPersisted = s;
  _lastPersistMs = millis();
}

// Takes scoreMutex itself, snapshots currentScore, and persists it. Safe to
// call from anywhere that is NOT already holding scoreMutex.
inline void persistNow() {
  xSemaphoreTake(scoreMutex, portMAX_DELAY);
  Score snapshot = currentScore;
  xSemaphoreGive(scoreMutex);
  _writeSnapshot(snapshot);
}

// Same intent as persistNow(), but for callers that already hold
// scoreMutex — score_actions.h's apply() holds it for its entire body, so
// its "reset" branch calling persistNow() (which re-takes the same,
// non-recursive mutex) would deadlock. Reads currentScore directly instead,
// which is safe precisely because the caller's own lock already guarantees
// nothing else can be mutating it concurrently.
inline void persistNowLocked() {
  _writeSnapshot(currentScore);
}

// Call from loop() (Core 1). Cheap when nothing changed (just a mutex take
// + memcmp, no flash access); when something did change, writes at most
// once per PERSIST_INTERVAL_MS so a flurry of quick points coalesces into
// one write instead of one per point.
inline void tick() {
  xSemaphoreTake(scoreMutex, portMAX_DELAY);
  Score cur = currentScore;
  xSemaphoreGive(scoreMutex);

  if (memcmp(&cur, &_lastPersisted, sizeof(Score)) == 0) return;
  if (millis() - _lastPersistMs < PERSIST_INTERVAL_MS) return;

  _writeSnapshot(cur);
}

// Call once from setup(), before the first LED::update(currentScore), so
// the very first frame drawn already reflects the restored score instead
// of a default-constructed 0-0. Returns true if a persisted state was
// found and restored. A size mismatch (e.g. a firmware update changed
// Score's layout) is treated the same as "nothing persisted yet" — safer
// than partially applying a stale/incompatible blob.
inline bool load() {
  Preferences prefs;
  prefs.begin("score", true);
  Score restored;
  size_t got = prefs.getBytes("state", &restored, sizeof(Score));
  prefs.end();

  if (got != sizeof(Score)) return false;

  xSemaphoreTake(scoreMutex, portMAX_DELAY);
  currentScore = restored;
  xSemaphoreGive(scoreMutex);

  _lastPersisted = restored;
  _lastPersistMs = millis();
  return true;
}

} // namespace ScorePersist
