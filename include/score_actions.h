#pragma once
#include <string.h>
#include <Preferences.h>
#include "score.h"
#include "led.h"
#include "score_logger.h"
#include "score_persist.h"
#include "mode.h"

extern Score currentScore;
extern SemaphoreHandle_t scoreMutex;

namespace ScoreActions {

static volatile uint32_t _rotationCount   = 0;
// Bumped on every "reset" command — lets the Firebase sync paths (main.cpp's
// firebaseTask, and the manager server's bridge via the WS state's
// resetCount) tell a real reset apart from a score that merely went to 0-0,
// since a reset wipes match-<channel>/event_history (event_history.h).
static volatile uint32_t _resetCount      = 0;
static volatile bool     _timerActive      = false;
static volatile uint32_t _timerStartMs     = 0;
static volatile bool     _timeoutActive    = false;
static volatile uint32_t _timeoutStartMs   = 0;
static volatile bool     _medicalActive    = false;
static volatile uint32_t _medicalStartMs   = 0;
// Team that called the running timeout/medical: 0 = A, 1 = B, -1 = none
// (plain "timeout"/"medical" from the portal/webapp/older pedals, or a
// break). Set by the "a/timeout", "b/medical"... commands - e.g. a pedal
// triple-click - and synced as match-<channel>/timer/team.
static volatile int8_t   _timerTeam        = -1;
// Whether the currently-running (or most recently run) break was started by
// an actual pedal long-press, as opposed to the portal's "Next Set" button
// or a remote/Firebase/CENTRAL-synced set change. Set wherever a break
// starts (see startBreakTimer(), applyTimerWithElapsed(), apply()'s "break"
// and didNextSet handling), read once at break-end (main.cpp) to decide
// whether the between-sets "SERVE?" prompt is worth showing — no pedal in
// use means nothing will ever press it, so skip it and go straight to the
// scoreboard, same as a non-pedal match already does pre-match.
static volatile bool     _breakIsPedalDriven = false;

// Battery saver
static volatile uint32_t _lastActivityMs   = 0;
static uint16_t        _dimTimeoutSec       = 300;
static volatile bool   _dimActive           = false;  // read by apply() on Core 0
static volatile bool   _wantsWake           = false;
static uint8_t         _preSleepBrightness  = 100;

// Pre-match flow: team-name marquee → "SERVE?" prompt → 0-0.
// _introShowing is published by main.cpp's loop (the only place that decides
// whether the marquee is on screen); any pedal press while it's true opens
// the server prompt instead of scoring. Picking a side closes the prompt and
// sets _introDismissed so the marquee doesn't come back over the 0-0 screen —
// cleared again by "reset" (or by main.cpp when a new match is detected).
static volatile bool _introShowing       = false;
static volatile bool _introDismissed     = false;
static volatile bool _serverSelectActive = false;
static volatile uint32_t _serverSelectStartMs = 0;
static constexpr uint32_t SERVER_SELECT_TIMEOUT_MS = 30UL * 1000UL;

// End-of-match validation: "OK? / HOLD" in turn with the set recap, on a
// board synced to Firebase (CENTRAL/FIREBASE) whose channel has
// match-<channel>/validation_required — set by RoundnetFranceConnect while it
// has a tournament match on that court (_validationRequired, fed by the sync
// paths); without it the match just ends on the recap. Holding a pedal fills the
// panel — the pedal sends hold/a|b ~300ms into the press, hold/end if it's
// released early, and its usual a|b/long at 2s, which confirms. A confirmed
// result gets a nonce; the sync paths write it as match-<channel>/
// result_validated {nonce, sets} and RoundnetFranceConnect answers in
// match-<channel>/rfc_result {nonce, status, message?, next?}. The validation
// belongs to the exact score it was given for (_valSig): any later change
// (portal correction, reset, Firebase) drops it. RFC's answer is still taken
// for VALIDATION_ANSWER_MS after the confirm though, because RFC records and
// then immediately hands the court to the next match (score back to 0-0, new
// team names) — the answer's "where do you play next" screen then runs on its
// own (_resultUntilMs) over the next match's marquee, until it times out or
// a pedal is pressed. No answer within ANSWER_TIMEOUT_MS (a tournament is
// expected to answer — the Cloud Function or RFC must have failed) turns into
// NO_ANSWER: a red "PAS DE REPONSE" then "OK? / HOLD" again, to retry with a
// new nonce. All guarded by scoreMutex.
enum class Validation : uint8_t { NONE, SENT, RECORDED, REJECTED, NO_ANSWER };
static volatile bool _validationRequired = false;
static Validation _valState     = Validation::NONE;
static uint32_t   _valNonce     = 0;  // current validation, 0 = none
static uint32_t   _valSig       = 0;  // _scoreSig() of the score it was given for
static uint32_t   _lastNonce    = 0;  // last confirmed nonce, kept after a drop
static uint32_t   _lastValMs    = 0;
static uint32_t   _valRev       = 0;  // bumped on confirm/answer — restarts the display cycle
static bool       _holdActive   = false;
static uint32_t   _holdStartMs  = 0;
static uint32_t   _holdEndMs    = 0;
static float      _holdEndP     = 0.0f;
static uint32_t   _resultNonce  = 0;  // nonce of the stored answer below
static uint32_t   _resultUntilMs = 0; // result screen shown until then, 0 = not shown
static bool       _resultRejected = false;
static char       _resultMsg[48]  = "";
static char       _nextA[24]      = "";
static char       _nextB[24]      = "";
static char       _resultNameA[TeamNames::MAX_LEN + 1] = "";
static char       _resultNameB[TeamNames::MAX_LEN + 1] = "";
static constexpr uint32_t HOLD_FILL_MS     = 1650;  // hold/x at ~300ms, x/long at 2000ms
static constexpr uint32_t HOLD_DRAIN_MS    = 400;
static constexpr uint32_t HOLD_LOST_MS     = HOLD_FILL_MS + 2500;  // hold/end never arrived
static constexpr uint32_t VALIDATION_RESET_GUARD_MS = 3000;
static constexpr uint32_t VALIDATION_ANSWER_MS      = 2UL * 60UL * 1000UL;
static constexpr uint32_t RESULT_SHOW_MS            = 90UL * 1000UL;
static constexpr uint32_t ANSWER_TIMEOUT_MS         = 30UL * 1000UL;

static constexpr uint32_t BREAK_DURATION_MS    = 3UL * 60UL * 1000UL;
static constexpr uint32_t TIMEOUT_COUNTDOWN_MS = 60UL * 1000UL;
static constexpr uint32_t MEDICAL_DURATION_MS  = 5UL * 60UL * 1000UL;

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
    Serial.println("[BATSAVER] waking — notifyActivity() fired while asleep");
    _wantsWake = false;
    _dimActive = false;
    SCORE_LOCK();
    LED::update(currentScore);
    SCORE_UNLOCK();
    return;
  }
  if (_dimTimeoutSec == 0 || _dimActive) return;
  uint32_t timeoutMs = (uint32_t)_dimTimeoutSec * 1000UL;
  if (millis() - _lastActivityMs >= timeoutMs) {
    _preSleepBrightness = LED::getBrightness();
    _dimActive = true;
  }
}

inline void setIntroShowing(bool v)     { _introShowing = v; }
inline bool isIntroDismissed()          { return _introDismissed; }
inline void clearIntroDismissed()       { _introDismissed = false; }
inline bool isServerSelectActive()      { return _serverSelectActive; }
inline void cancelServerSelect()        { _serverSelectActive = false; }

// Opens the "SERVE?" prompt outside the pre-match intro flow — used when the
// break timer between sets ends (main.cpp's loop) so the next set also gets
// an explicit server pick instead of silently carrying over whichever team
// served the set that just finished.
inline void startServerSelect() {
  _serverSelectActive  = true;
  _serverSelectStartMs = millis();
}

// No side picked within SERVER_SELECT_TIMEOUT_MS: close the "SERVE?" prompt
// and bring the team-name marquee back. Called from main.cpp's loop.
inline void tickServerSelect() {
  if (_serverSelectActive && millis() - _serverSelectStartMs >= SERVER_SELECT_TIMEOUT_MS) {
    _serverSelectActive = false;
    _introDismissed     = false;
  }
}

// ── End-of-match validation (see Validation above) ──────────────────────────

inline bool validationEnabled() { return !Mode::isLocal() && _validationRequired; }
inline void setValidationRequired(bool required) { _validationRequired = required; }

// Identifies a score: every field that ends up in result_validated's sets.
inline uint32_t _scoreSig(const Score& s) {
  const uint8_t v[] = {s.scoreA, s.scoreB, s.setA, s.setB, s.histA[0], s.histB[0],
                       s.histA[1], s.histB[1], s.histA[2], s.histB[2]};
  uint32_t h = 2166136261u;
  for (uint8_t b : v) h = (h ^ b) * 16777619u;
  return h;
}

// Callers of the *Locked helpers hold scoreMutex.
inline bool _validatedLocked() { return _valNonce != 0 && _valSig == _scoreSig(currentScore); }
// Waiting for a confirm: never validated, or validated without any answer.
inline bool _validationPendingLocked() {
  return validationEnabled() && isMatchOver(currentScore) &&
         (!_validatedLocked() || _valState == Validation::NO_ANSWER);
}

// Fill level of the "OK? / HOLD" screen: rises over HOLD_FILL_MS while held,
// drains over HOLD_DRAIN_MS once released.
inline float _holdProgressLocked(uint32_t now) {
  if (_holdActive) {
    uint32_t held = now - _holdStartMs;
    if (held < HOLD_LOST_MS) return held >= HOLD_FILL_MS ? 1.0f : (float)held / HOLD_FILL_MS;
    _holdActive = false;  // hold/end lost on the way: drain from full
    _holdEndP   = 1.0f;
    _holdEndMs  = now;
  }
  float p = _holdEndP - (float)(now - _holdEndMs) / HOLD_DRAIN_MS;
  return p > 0.0f ? p : 0.0f;
}

inline float holdProgress() {
  SCORE_LOCK();
  float p = _holdProgressLocked(millis());
  SCORE_UNLOCK();
  return p;
}

// hold/a, hold/b, hold/end from the pedal. Never wakes the display (the long
// press that follows does, and is swallowed like any press made while
// asleep) and never touches timers or the score. Returns false either way:
// nothing changed that a state push would need to carry.
inline bool _applyHold(const char* arg) {
  if (_dimActive) return false;
  SCORE_LOCK();
  uint32_t now = millis();
  if (strcmp(arg, "end") == 0) {
    if (_holdActive) {
      _holdEndP    = _holdProgressLocked(now);
      _holdEndMs   = now;
      _holdActive  = false;
    }
  } else if (!_holdActive && _validationPendingLocked()) {
    // Picks up from a fill that's still draining instead of jumping back to 0.
    float p = _holdProgressLocked(now);
    _holdStartMs    = now - (uint32_t)(p * HOLD_FILL_MS);
    _holdActive     = true;
    _lastActivityMs = now;
  }
  SCORE_UNLOCK();
  return false;
}

// Confirms the current (match-over) score: a pedal long press on the "OK? /
// HOLD" screen, or the portal's "Validate Score" button.
inline void _confirmValidationLocked() {
  _valNonce   = esp_random() | 1;
  _valSig     = _scoreSig(currentScore);
  _valState   = Validation::SENT;
  _lastNonce  = _valNonce;
  _lastValMs  = millis();
  _valRev++;
  _holdActive = false;
  _holdEndP   = 0.0f;
  const TeamNames::Names& n = TeamNames::get();
  strlcpy(_resultNameA, n.teamA[0] ? n.teamA : "TEAM A", sizeof(_resultNameA));
  strlcpy(_resultNameB, n.teamB[0] ? n.teamB : "TEAM B", sizeof(_resultNameB));
  Serial.printf("[VALIDATE] result confirmed, nonce %08lx\n", (unsigned long)_valNonce);
}

// Portal entry point — false when there's nothing to validate (match not
// over, already validated, or LOCAL mode).
inline bool confirmValidation() {
  notifyActivity();
  SCORE_LOCK();
  bool pending = _validationPendingLocked();
  if (pending) _confirmValidationLocked();
  SCORE_UNLOCK();
  return pending;
}

// For the portal: "none" (nothing to validate), "pending" (match over, not
// validated yet), "failed" (no answer — retry), or the validation's state.
inline const char* validationStatus() {
  SCORE_LOCK();
  const char* st = _validatedLocked() && _valState == Validation::NO_ANSWER ? "failed"
                 : _validationPendingLocked() ? "pending"
                 : !_validatedLocked()        ? "none"
                 : _valState == Validation::RECORDED ? "recorded"
                 : _valState == Validation::REJECTED ? "rejected"
                 : "sent";
  SCORE_UNLOCK();
  return st;
}

// Nonce of the validation given for `s`, 0 if none — what the sync paths
// write as result_validated (null for 0).
inline uint32_t validatedNonce(const Score& s) {
  SCORE_LOCK();
  uint32_t n = (_valNonce != 0 && _valSig == _scoreSig(s)) ? _valNonce : 0;
  SCORE_UNLOCK();
  return n;
}

// Nonce whose answer (rfc_result) is still awaited or may still change, 0 if
// none — the sync paths only read rfc_result while this is set.
inline uint32_t answerNonce() {
  SCORE_LOCK();
  uint32_t n = (_lastNonce != 0 && millis() - _lastValMs < VALIDATION_ANSWER_MS) ? _lastNonce : 0;
  SCORE_UNLOCK();
  return n;
}

// RoundnetFranceConnect's answer for `nonce`. Ignored unless it's for the
// last confirmed result; the same answer read again changes nothing, an
// updated one (e.g. a next court that became known) shows the screen again.
inline void applyResult(uint32_t nonce, bool rejected, const char* msg,
                        const char* nextA, const char* nextB) {
  SCORE_LOCK();
  if (nonce == 0 || nonce != _lastNonce) { SCORE_UNLOCK(); return; }
  bool same = _resultNonce == nonce && _resultRejected == rejected &&
              strcmp(_resultMsg, msg) == 0 && strcmp(_nextA, nextA) == 0 && strcmp(_nextB, nextB) == 0;
  if (!same) {
    _resultNonce    = nonce;
    _resultRejected = rejected;
    strlcpy(_resultMsg, msg,   sizeof(_resultMsg));
    strlcpy(_nextA,     nextA, sizeof(_nextA));
    strlcpy(_nextB,     nextB, sizeof(_nextB));
    if (_valNonce == nonce) _valState = rejected ? Validation::REJECTED : Validation::RECORDED;
    _resultUntilMs = (millis() + RESULT_SHOW_MS) | 1;
    _valRev++;
  }
  SCORE_UNLOCK();
  if (!same) notifyActivity();
}

// What the display shows for the current validation, NONE once dropped.
inline Validation validationState() {
  SCORE_LOCK();
  Validation v = _validatedLocked() ? _valState : Validation::NONE;
  SCORE_UNLOCK();
  return v;
}

inline uint32_t validationRev() { return _valRev; }

inline bool resultScreenActive() {
  SCORE_LOCK();
  bool on = _resultUntilMs != 0;
  SCORE_UNLOCK();
  return on;
}

struct ResultView {
  bool rejected;
  char msg[sizeof(_resultMsg)];
  char nextA[sizeof(_nextA)];
  char nextB[sizeof(_nextB)];
  char nameA[sizeof(_resultNameA)];
  char nameB[sizeof(_resultNameB)];
};

inline void resultView(ResultView& v) {
  SCORE_LOCK();
  v.rejected = _resultRejected;
  strlcpy(v.msg,   _resultMsg,   sizeof(v.msg));
  strlcpy(v.nextA, _nextA,       sizeof(v.nextA));
  strlcpy(v.nextB, _nextB,       sizeof(v.nextB));
  strlcpy(v.nameA, _resultNameA, sizeof(v.nameA));
  strlcpy(v.nameB, _resultNameB, sizeof(v.nameB));
  SCORE_UNLOCK();
}

// Called from main.cpp's loop: drops a validation whose score changed, turns
// an unanswered one into NO_ANSWER, drops a hold that no longer has anything
// to validate, and ends an expired result screen.
inline void tickValidation() {
  SCORE_LOCK();
  if (_valNonce != 0 && !_validatedLocked()) { _valNonce = 0; _valState = Validation::NONE; }
  if (_valNonce != 0 && _valState == Validation::SENT && millis() - _lastValMs >= ANSWER_TIMEOUT_MS) {
    _valState = Validation::NO_ANSWER;
    _valRev++;
    Serial.println("[VALIDATE] no answer from the tournament app");
  }
  if (_holdActive && !_validationPendingLocked()) { _holdActive = false; _holdEndP = 0.0f; }
  if (_resultUntilMs != 0 && (int32_t)(millis() - _resultUntilMs) >= 0) _resultUntilMs = 0;
  SCORE_UNLOCK();
}

// Physical pedal presses only (a|b / single|double|long). Portal/webapp use
// a/inc, a/dec etc., which keep scoring directly even during the marquee.
inline bool _isPedalPress(const char* cmd, bool& teamA) {
  if (strcmp(cmd, "a/single") == 0 || strcmp(cmd, "a/double") == 0 || strcmp(cmd, "a/long") == 0) { teamA = true;  return true; }
  if (strcmp(cmd, "b/single") == 0 || strcmp(cmd, "b/double") == 0 || strcmp(cmd, "b/long") == 0) { teamA = false; return true; }
  return false;
}

inline void     triggerRotation()     { _rotationCount++; }
inline uint32_t getRotationCount()    { return _rotationCount; }
// Only called for a remote/Firebase-detected set change (main.cpp) — never
// pedal-driven, see _breakIsPedalDriven above.
inline void     startBreakTimer()     { _timerActive = true; _timerStartMs = millis(); _breakIsPedalDriven = false; }
inline bool     isBreakPedalDriven()  { return _breakIsPedalDriven; }

inline bool isBreakTimerActive() { return _timerActive; }
inline uint32_t breakTimerRemainingMs() {
  if (!_timerActive) return 0;
  uint32_t elapsed = millis() - _timerStartMs;
  if (elapsed >= BREAK_DURATION_MS) { _timerActive = false; return 0; }
  return BREAK_DURATION_MS - elapsed;
}
// How long the currently-running break has already been going, or 0 if none
// — used to carry a break's elapsed time across applyFromDatabase() (which
// unconditionally clears _timerActive as part of applying a wholesale score
// sync), so a break that was already correctly running isn't reset back to
// 0 elapsed by a same-transition score catch-up arriving in a later tick.
inline uint32_t breakTimerElapsedMs() { return _timerActive ? (millis() - _timerStartMs) : 0; }

// Timer keys: a timer command/state is "<type>" or "<a|b>/<type>" — the
// same string is the apply() command, activeTimerKey()'s return value and
// what firebase.h/main.cpp compare against Firebase. Splits it into the
// bare type and team (0/1, or -1 when there's no team prefix).
inline const char* _splitTimerKey(const char* key, int8_t& team) {
  team = -1;
  if (key && (key[0] == 'a' || key[0] == 'b') && key[1] == '/') {
    team = (key[0] == 'a') ? 0 : 1;
    return key + 2;
  }
  return key ? key : "";
}

inline uint32_t resetCount() { return _resetCount; }

inline bool isTimeoutActive() { return _timeoutActive; }
inline uint32_t timeoutCountdownMs() {
  if (!_timeoutActive) return 0;
  uint32_t elapsed = millis() - _timeoutStartMs;
  if (elapsed >= TIMEOUT_COUNTDOWN_MS) { _timeoutActive = false; return 0; }
  return TIMEOUT_COUNTDOWN_MS - elapsed;
}

inline bool isMedicalActive() { return _medicalActive; }
inline uint32_t medicalCountdownMs() {
  if (!_medicalActive) return 0;
  uint32_t elapsed = millis() - _medicalStartMs;
  if (elapsed >= MEDICAL_DURATION_MS) { _medicalActive = false; return 0; }
  return MEDICAL_DURATION_MS - elapsed;
}

// Which of timeout/medical/break is currently running, or nullptr if none —
// same priority order as the LED display loop in main.cpp (timeout > medical
// > break), used by firebase.h to decide what (if anything) to write to the
// shared RTDB "timer" node.
inline const char* activeTimerType() {
  if (_timeoutActive) return "timeout";
  if (_medicalActive) return "medical";
  if (_timerActive)   return "break";
  return nullptr;
}

// Team of the running timeout/medical (0 = A, 1 = B), -1 if none/break.
inline int8_t activeTimerTeam() {
  return (_timeoutActive || _medicalActive) ? _timerTeam : -1;
}

// activeTimerType() with its team prefix, e.g. "a/timeout", "b/medical",
// "timeout" (no team), "break" — or nullptr when nothing is running.
inline const char* activeTimerKey() {
  const char* type = activeTimerType();
  if (!type) return nullptr;
  int8_t team = activeTimerTeam();
  if (team < 0) return type;
  if (_timeoutActive) return team == 0 ? "a/timeout" : "b/timeout";
  return team == 0 ? "a/medical" : "b/medical";
}

// Another writer switched which team the already-running timeout/medical
// belongs to - change the team without restarting the countdown.
inline void setTimerTeam(int8_t team) {
  SCORE_LOCK();
  _timerTeam = team;
  SCORE_UNLOCK();
}

// Start a timer as if it had already been running for elapsedMs — used when
// picking up a timer/timeout/medical that was already active in Firebase on
// a fresh read (boot, WiFi reconnect, Firebase channel switch), so the board
// shows the actual remaining time instead of either a full fresh countdown
// (wrong — it wasn't just started) or nothing at all (the old fallback, for
// when the elapsed time genuinely couldn't be determined). Reuses the same
// remaining-ms getters (breakTimerRemainingMs() etc.), which already clamp
// to 0/inactive once elapsed passes the type's duration — so passing an
// elapsedMs at or past that duration self-corrects on the very next read
// instead of needing to be filtered out here.
inline void applyTimerWithElapsed(const char* key, uint32_t elapsedMs) {
  int8_t team;
  const char* type = _splitTimerKey(key, team);
  SCORE_LOCK();
  _timerActive   = false;
  _timeoutActive = false;
  _medicalActive = false;
  _timerTeam     = (strcmp(type, "break") == 0) ? -1 : team;
  uint32_t startMs = (elapsedMs < millis()) ? (millis() - elapsedMs) : 0;
  if (strcmp(type, "timeout") == 0)      { _timeoutActive = true; _timeoutStartMs = startMs; }
  else if (strcmp(type, "medical") == 0) { _medicalActive = true; _medicalStartMs = startMs; }
  else if (strcmp(type, "break") == 0)   { _timerActive   = true; _timerStartMs   = startMs; _breakIsPedalDriven = false; }
  LED::update(currentScore);
  SCORE_UNLOCK();
}

// Apply a score state received from Firebase — cancels break/timeout timers, updates display
inline void applyFromDatabase(const Score& db) {
  notifyActivity();
  SCORE_LOCK();
  _timerActive   = false;
  _timeoutActive = false;
  _medicalActive = false;
  currentScore = db;
  LED::update(currentScore);
  SCORE_UNLOCK();
}

inline bool apply(const char* cmd) {
  // Not "a/"/"b/"-prefixed on purpose: older firmware treats any of those as
  // a game command (cancels a running timeout) — "hold/" it just ignores.
  if (strncmp(cmd, "hold/", 5) == 0) return _applyHold(cmd + 5);
  const bool wasAsleep = _dimActive;
  notifyActivity();
  SCORE_LOCK();

  // A pedal press while the display sleeps only wakes it. The players can't
  // see the board's state yet, and main.cpp's loop doesn't refresh
  // _introShowing while asleep — so the press used to open "SERVE?" (or
  // score a point) instead of letting the team-name marquee come back first.
  bool wakeTeamA;
  if (wasAsleep && _isPedalPress(cmd, wakeTeamA)) {
    SCORE_UNLOCK();
    return true;
  }

  // Timer commands (timeout/break/medical) are mutually exclusive — starting
  // one always cancels whichever of the other two was running. Timeout and
  // medical take an optional team prefix ("a/timeout", "b/medical" — a
  // pedal triple-click sends "<a|b>/timeout"); checked before the generic
  // "a/"/"b/" game-command handling below so they never cancel/score.
  int8_t timerTeam;
  const char* timerType = _splitTimerKey(cmd, timerTeam);
  if (strcmp(timerType, "timeout") == 0 || strcmp(timerType, "medical") == 0) {
    bool isTimeout = (timerType[0] == 't');
    // Same team re-triggering its already-running timer (e.g. a second
    // triple-click): keep the countdown going instead of restarting it.
    if ((isTimeout ? _timeoutActive : _medicalActive) && _timerTeam == timerTeam) {
      SCORE_UNLOCK();
      return true;
    }
    _timerActive    = false;
    _timeoutActive  = isTimeout;
    _medicalActive  = !isTimeout;
    if (isTimeout) _timeoutStartMs = millis();
    else           _medicalStartMs = millis();
    _timerTeam      = timerTeam;
    SCORE_UNLOCK();
    return true;
  }
  if (strcmp(cmd, "break") == 0) {
    _timeoutActive  = false;
    _medicalActive  = false;
    _timerTeam      = -1;
    _timerActive    = true;
    _timerStartMs   = millis();
    // The portal's manual break button and every relayed CENTRAL/remote
    // "break" command land here — never a pedal command (see
    // _isPedalPress()), so this is never pedal-driven.
    _breakIsPedalDriven = false;
    SCORE_UNLOCK();
    return true;
  }

  // Cancel-only: stops whichever of break/timeout/medical is running, with no
  // score side effect. Lets an external source (ObserverTool cancelling its
  // own synced countdown early over ESP-NOW, or a future portal button) stop
  // a timer the same way a known game command already does as a side effect.
  if (strcmp(cmd, "stoptimer") == 0) {
    bool wasActive = _timerActive || _timeoutActive || _medicalActive;
    _timerActive    = false;
    _timeoutActive  = false;
    _medicalActive  = false;
    if (wasActive) LED::update(currentScore);
    SCORE_UNLOCK();
    return wasActive;
  }

  // Only known game commands cancel active timers — unknown strings are silently ignored
  bool isKnown =
      strncmp(cmd, "a/", 2) == 0 ||
      strncmp(cmd, "b/", 2) == 0 ||
      strcmp(cmd, "nextset") == 0 ||
      strcmp(cmd, "reset")   == 0;
  if (!isKnown) {
    SCORE_UNLOCK();
    return false;
  }

  // Known game command cancels break timer without scoring (reset is exempt — it executes regardless)
  if (_timerActive) {
    _timerActive = false;
    if (strcmp(cmd, "reset") != 0) {
      LED::update(currentScore);
      SCORE_UNLOCK();
      return true;
    }
  }

  // Known game command cancels timeout without scoring (reset is exempt — it executes regardless)
  if (_timeoutActive) {
    _timeoutActive = false;
    if (strcmp(cmd, "reset") != 0) {
      LED::update(currentScore);
      SCORE_UNLOCK();
      return true;
    }
  }

  // Known game command cancels medical timer without scoring (reset is exempt — it executes regardless)
  if (_medicalActive) {
    _medicalActive = false;
    if (strcmp(cmd, "reset") != 0) {
      LED::update(currentScore);
      SCORE_UNLOCK();
      return true;
    }
  }

  // Pre-match server selection (see _introShowing above).
  bool pedalTeamA = false;
  bool isPedal = _isPedalPress(cmd, pedalTeamA);

  // End-of-match validation (see Validation above). Any pedal press closes a
  // result screen — typically the next match's players arriving — and does
  // nothing else. Once confirmed, pedals no longer change the result (the
  // portal still can, which drops the validation) — unless no answer came
  // (NO_ANSWER), where a long press retries. The pedal's very long
  // press (reset) fires 2s after the long press that confirmed, so a player
  // still holding would wipe the result: ignored right after a confirm.
  if (isPedal && _resultUntilMs != 0) {
    _resultUntilMs = 0;
    SCORE_UNLOCK();
    return true;
  }
  if (isPedal && _validatedLocked() && _valState != Validation::NO_ANSWER) {
    SCORE_UNLOCK();
    return false;
  }
  if (isPedal && strstr(cmd, "/long") && _validationPendingLocked()) {
    _confirmValidationLocked();
    SCORE_UNLOCK();
    return true;
  }
  if (strcmp(cmd, "reset") == 0 && _lastValMs != 0 && millis() - _lastValMs < VALIDATION_RESET_GUARD_MS) {
    SCORE_UNLOCK();
    return false;
  }

  if (isPedal && _serverSelectActive) {
    _serverSelectActive = false;
    currentScore.firstServer = pedalTeamA ? 0 : 1;
    LED::update(currentScore);
    SCORE_UNLOCK();
    return true;
  }
  if (isPedal && _introShowing) {
    _introShowing       = false;
    _introDismissed     = true;
    _serverSelectActive = true;
    _serverSelectStartMs = millis();
    SCORE_UNLOCK();
    return true;
  }
  if (strcmp(cmd, "reset") == 0) {
    _serverSelectActive = false;
    _introDismissed     = false;
    _resultUntilMs      = 0;
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
  else if (strcmp(cmd, "reset")   == 0) { currentScore.reset(); ScoreLogger::reset(); ScorePersist::persistNowLocked(); _resetCount++; }
  else { changed = false; ok = false; }

  if (didIncrement) {
    if ((currentScore.scoreA + currentScore.scoreB) % 4 == 3) {
      _rotationCount++;
    }
  }
  if (didNextSet) {
    // Break between sets only — none once the match is over (BO1 never, 2
    // sets after the first, BO3 until someone has 2); the end-of-match
    // summary (main.cpp) takes over instead.
    bool startTimer = !isMatchOver(currentScore);
    if (startTimer) {
      _timerActive  = true;
      _timerStartMs = millis();
      // isPedal is only true for an actual a/long|b/long press — the
      // portal's "Next Set" button sends the plain "nextset" string, which
      // isn't a pedal command (see _isPedalPress()), so this correctly comes
      // out false for a portal-driven set transition.
      _breakIsPedalDriven = isPedal;
    }
  }

  if (changed) {
    LED::update(currentScore);
    ScoreLogger::log(currentScore);
  }
  SCORE_UNLOCK();
  return ok;
}

} // namespace ScoreActions
