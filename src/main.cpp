/**
 * Roundnet Scoreboard
 */

#include <Arduino.h>
#include "config.h"
#include "mode.h"
#include "score.h"
#include "team_names.h"
#include "led.h"
#include "wifi_mgr.h"
#include "firebase.h"
#include "portal.h"
#include "espnow_handler.h"
#include "score_persist.h"

Score currentScore;
SemaphoreHandle_t scoreMutex = NULL;
TaskHandle_t firebaseTaskHandle = nullptr;
// Updated at the top of every firebaseTask loop iteration, before any
// potentially-blocking WiFi/HTTP call — lets Portal::tick() (Core 1) detect
// a wedged task (e.g. a WiFiClientSecure connect/handshake that never
// returns, which has been observed on ESP32 right after a fresh STA
// association, before ARP/routing has fully settled) and recycle it, since
// a task stuck inside a blocking call can't recover itself.
volatile uint32_t firebaseLastHeartbeat = 0;

// ── Firebase task (Core 0, Firebase mode only) ────────────────────────────────

void firebaseTask(void*) {
  const uint32_t INTERVAL_ERR = 15000;
  const uint8_t  MAX_ERRORS   = 5;

  uint32_t lastRead          = 0;
  uint8_t  consecutiveErrors = 0;
  // Held off until the first Firebase read succeeds — see its use below.
  // Without this, a board that just booted (currentScore restored from NVS
  // by ScorePersist::load(), team/player names restored from NVS by
  // TeamNames::init() — both possibly stale relative to whatever happened
  // in Firebase while this board was off/rebooting) would compare its own
  // restored state against lastWritten/lastWrittenNames's zeroed/empty
  // defaults on the very first loop iteration, see a "change", and push its
  // own guess up to Firebase BEFORE ever reading what's actually there —
  // silently overwriting genuinely newer data from another writer with
  // stale local memory. Reading first, unconditionally, before ever
  // considering a push, is what makes "board reboots, then reads whatever's
  // in the DB" actually true for this mode.
  bool didInitialRead = false;

  Score   lastWritten   = {};
  uint8_t lastWrittenWP  = 255;  // force first write of winPoints
  uint8_t lastWrittenHC  = 255;  // force first write of hardcap
  uint8_t lastWrittenFmt = 255;  // force first write of format
  uint8_t lastWrittenFS = 255;
  uint8_t lastWrittenFSSet = 255;  // active-set index (setA+setB) writeFirstServer last wrote for
  TeamNames::Names lastWrittenNames = {};
  // "" = no active timer, matching this board's always-clear-on-boot local
  // state — unlike score/names there's no NVS-restored guess to protect
  // against, so no special force-write-first sentinel is needed here.
  String lastWrittenTimerType    = "";
  String lastSeenRemoteTimerType = "";
  // True from boot and after every WiFi drop, until the first successful
  // score/timer read after (re)connecting. A set change or a running "break"
  // seen on that first read happened at some unknown point while we were
  // offline — the board can't know how much of the break is left, so it just
  // shows the score instead of starting a fresh 3-minute countdown.
  bool freshScoreRead = true;
  bool freshTimerRead = true;

  for (;;) {
    firebaseLastHeartbeat = millis();
    WiFiMgr::tick();

    if (WiFiMgr::isOnline()) {
      xSemaphoreTake(scoreMutex, portMAX_DELAY);
      Score local = currentScore;
      xSemaphoreGive(scoreMutex);

      // Push local changes to Firebase — gated on didInitialRead, see comment
      // on its declaration above.
      if (didInitialRead &&
          (local.scoreA != lastWritten.scoreA || local.scoreB != lastWritten.scoreB ||
           local.setA   != lastWritten.setA   || local.setB   != lastWritten.setB)) {
        if (Firebase::writeScore(local)) {
          lastWritten = local;
          lastRead = millis();
        }
      }
      if (didInitialRead && local.winPoints != lastWrittenWP) {
        if (Firebase::writeWinPoints(local.winPoints)) lastWrittenWP = local.winPoints;
      }
      if (didInitialRead && local.hardcap != lastWrittenHC) {
        if (Firebase::writeHardcap(local.hardcap)) lastWrittenHC = local.hardcap;
      }
      if (didInitialRead && local.format != lastWrittenFmt) {
        if (Firebase::writeFormat(local.format)) lastWrittenFmt = local.format;
      }
      // Re-fires on a new set too, not just when firstServer's raw value
      // changes — nextSet() (score.h) never touches firstServer, it just
      // carries the previous set's value forward, and a scorekeeper very
      // commonly re-declares the SAME team to serve first in the next set
      // (nothing forces alternation). Gating on value-change alone left
      // that new set's own score/set_N node in Firebase with no
      // starting_server at all whenever that happened — silently dropping
      // the live hub's serve indicator for the whole set, since it derives
      // serving from that per-set field and had nothing carried over to
      // read (each set is a distinct node, not inherited from the last).
      uint8_t activeSetIdx = local.setA + local.setB;
      if (didInitialRead && (local.firstServer != lastWrittenFS || activeSetIdx != lastWrittenFSSet)) {
        if (Firebase::writeFirstServer(local)) { lastWrittenFS = local.firstServer; lastWrittenFSSet = activeSetIdx; }
      }

      TeamNames::Names names = TeamNames::get();
      if (didInitialRead &&
          (strcmp(names.teamA, lastWrittenNames.teamA)       != 0 ||
           strcmp(names.teamB, lastWrittenNames.teamB)       != 0 ||
           strcmp(names.playerA1, lastWrittenNames.playerA1) != 0 ||
           strcmp(names.playerA2, lastWrittenNames.playerA2) != 0 ||
           strcmp(names.playerB1, lastWrittenNames.playerB1) != 0 ||
           strcmp(names.playerB2, lastWrittenNames.playerB2) != 0)) {
        if (Firebase::writeTeamNames(names)) lastWrittenNames = names;
      }

      // Push this board's own active timer (or its absence, on natural
      // expiry/cancel) up to Firebase — see firebase.h's writeTimerState.
      const char* localTimerType = ScoreActions::activeTimerType();
      String localTimerStr = localTimerType ? String(localTimerType) : String("");
      if (didInitialRead && localTimerStr != lastWrittenTimerType) {
        if (Firebase::writeTimerState(localTimerType)) {
          lastWrittenTimerType    = localTimerStr;
          // We just authored this value ourselves — treat it as already
          // "seen" so the next read tick doesn't re-apply(type) on top of
          // the countdown we just started (which would restart it from
          // scratch every poll interval instead of counting down).
          lastSeenRemoteTimerType = localTimerStr;
        }
      }

      // Too many errors: pause 30s
      if (consecutiveErrors >= MAX_ERRORS) {
        Serial.println("[Firebase] Too many errors — pausing 30s");
        vTaskDelay(30000 / portTICK_PERIOD_MS);
        consecutiveErrors = 0;
        continue;
      }

      // Periodically read Firebase for external changes — always due
      // immediately (ignoring the poll interval) until the very first read
      // of this boot succeeds, so a freshly-booted board's NVS-restored
      // guess never sits around unconfirmed for a full poll interval before
      // being checked against reality.
      uint32_t interval = consecutiveErrors > 0 ? INTERVAL_ERR : Firebase::getPollIntervalMs();
      uint32_t now = millis();
      if (!didInitialRead || (now - lastRead) >= interval) {
        lastRead = now;
        Score db = local;
        if (Firebase::readScore(db)) {
          consecutiveErrors = 0;
          didInitialRead = true;
          if (db.scoreA      != lastWritten.scoreA     || db.scoreB     != lastWritten.scoreB   ||
              db.setA        != lastWritten.setA        || db.setB       != lastWritten.setB     ||
              db.firstServer != lastWritten.firstServer || db.winPoints  != lastWritten.winPoints ||
              db.hardcap     != lastWritten.hardcap      || db.format     != lastWritten.format) {

            // Detect set change (active set number increased)
            bool setJustEnded = !freshScoreRead &&
                                (db.setA + db.setB) > (lastWritten.setA + lastWritten.setB);

            // Detect rotation: check if score total crossed a multiple-of-4+3 threshold
            if (!setJustEnded) {
              uint8_t oldTotal = lastWritten.scoreA + lastWritten.scoreB;
              uint8_t newTotal = db.scoreA + db.scoreB;
              if (newTotal > oldTotal) {
                for (uint8_t t = oldTotal + 1; t <= newTotal; t++) {
                  if (t % 4 == 3) { ScoreActions::triggerRotation(); break; }
                }
              }
            }

            ScoreActions::applyFromDatabase(db);
            if (setJustEnded) ScoreActions::startBreakTimer();

            lastWritten    = db;
            lastWrittenWP  = db.winPoints;
            lastWrittenHC  = db.hardcap;
            lastWrittenFmt = db.format;
            lastWrittenFS  = db.firstServer;
          }
          freshScoreRead = false;
        } else {
          consecutiveErrors++;
          Serial.printf("[Firebase] Error %d/%d\n", consecutiveErrors, MAX_ERRORS);
        }

        // Names, too: pick up anything set through another tool (worlds-live
        // editor UI, Fwango bridge) or another board flow. Compared against
        // lastWrittenNames (what we believe Firebase currently holds), not
        // TeamNames::get() directly, so reading back exactly what we just
        // wrote doesn't bounce back down and overwrite in-progress local edits.
        TeamNames::Names dbNames;
        if (Firebase::readTeamNames(dbNames)) {
          if (strcmp(dbNames.teamA, lastWrittenNames.teamA)       != 0 ||
              strcmp(dbNames.teamB, lastWrittenNames.teamB)       != 0 ||
              strcmp(dbNames.playerA1, lastWrittenNames.playerA1) != 0 ||
              strcmp(dbNames.playerA2, lastWrittenNames.playerA2) != 0 ||
              strcmp(dbNames.playerB1, lastWrittenNames.playerB1) != 0 ||
              strcmp(dbNames.playerB2, lastWrittenNames.playerB2) != 0) {
            TeamNames::set(dbNames.teamA, dbNames.teamB,
                            dbNames.playerA1, dbNames.playerA2,
                            dbNames.playerB1, dbNames.playerB2);
            lastWrittenNames = dbNames;
          }
        }

        // Pick up a timer started/cancelled from another writer (scoreboard/'s
        // input.html, or the manager server relaying a CENTRAL-mode board on
        // the same channel). Read AFTER the score block above so that, if a
        // set transition and a timer change land in the same tick,
        // applyFromDatabase()'s unconditional timer-flags-clear (score.h)
        // never wipes a timer we're about to (re)apply right here.
        String remoteTimerType;
        if (Firebase::readTimerState(remoteTimerType)) {
          if (freshTimerRead && remoteTimerType == "break" && !ScoreActions::isBreakTimerActive()) {
            // Stale/unknown-age break (see freshScoreRead): don't start it
            // here, and don't clear it in Firebase either — just remember it
            // as seen so later polls don't pick it up as a new change.
            lastSeenRemoteTimerType = remoteTimerType;
          } else if (remoteTimerType != lastSeenRemoteTimerType) {
            ScoreActions::apply(remoteTimerType.length() > 0 ? remoteTimerType.c_str() : "stoptimer");
            lastSeenRemoteTimerType = remoteTimerType;
            lastWrittenTimerType    = remoteTimerType;
          }
          freshTimerRead = false;
        }
      }
    } else {
      consecutiveErrors = 0;
      freshScoreRead = true;
      freshTimerRead = true;
    }

    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
  scoreMutex = xSemaphoreCreateMutex();

  // LEDs first: boot animation plays while USB CDC enumerates
  LED::init();
  LED::bootAnimation();

  Serial.begin(115200);
  delay(200);

  Serial.println("\n=== ROUNDNET SCOREBOARD ===");
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());

  // 1. Mode — must run before WiFiMgr (guards STA connect)
  Serial.println("[1/4] Init Mode...");
  Mode::init();
  ScoreActions::initBatterySaver();
  TeamNames::init();
  // Restore the last known score/sets/settings before anything renders, so
  // the boot animation is followed by the real state instead of 0-0 — see
  // score_persist.h for why this matters most in LOCAL mode.
  ScorePersist::load();

  // 2. WiFi (AP always on; STA skipped in Local mode)
  Serial.println("[2/4] Init WiFi...");
  WiFiMgr::init();

  // 3. ESP-NOW (requires WiFi up)
  Serial.println("[3/4] Init ESP-NOW...");
  EspNow::init();

  // 4. Portal + mode-specific network feature
  Serial.println("[4/4] Init Portal...");
  Portal::init();

  // Network services start in Portal::tick() once WiFi connects, not here.

  LED::update(currentScore);

  Serial.println("\n=== READY ===");
  Serial.printf("AP: %s | Portal: http://%s\n",
    WiFiMgr::getScoreboardId().c_str(), WiFiMgr::apIP().c_str());
}

// ── Loop (Core 1) ─────────────────────────────────────────────────────────────

void loop() {
  if (Mode::isCentral()) WsClient::tick();
  // Firebase mode: WiFiMgr::tick() runs inside firebaseTask on Core 0
  if (!Mode::isFirebase()) WiFiMgr::tick();

  Portal::tick();
  EspNow::tick();
  ScoreActions::tickBatterySaver();
  ScorePersist::tick();

  // Sleep animation: replaces all display logic while inactive
  if (ScoreActions::isDimActive()) {
    static uint32_t _lastSleepFrame = 0;
    uint32_t _now = millis();
    if (_now - _lastSleepFrame >= 33) {
      _lastSleepFrame = _now;
      xSemaphoreTake(scoreMutex, portMAX_DELAY);
      LED::showSleepAnimation();
      xSemaphoreGive(scoreMutex);
    }
    delay(10);
    return;
  }

  static bool     prevTimeoutActive = false;
  static uint32_t lastTimeoutUpdate = 0;
  bool timeoutActive = ScoreActions::isTimeoutActive();

  if (timeoutActive) {
    uint32_t remaining = ScoreActions::timeoutCountdownMs();
    uint32_t now       = millis();
    if (remaining > 0 && now - lastTimeoutUpdate >= 33) {
      lastTimeoutUpdate = now;
      xSemaphoreTake(scoreMutex, portMAX_DELAY);
      LED::showTimeoutDisplay(remaining);
      xSemaphoreGive(scoreMutex);
    }
  } else if (prevTimeoutActive) {
    xSemaphoreTake(scoreMutex, portMAX_DELAY);
    LED::update(currentScore);
    xSemaphoreGive(scoreMutex);
  }
  prevTimeoutActive = timeoutActive;

  static bool     prevMedicalActive = false;
  static uint32_t lastMedicalUpdate = 0;
  bool medicalActive = !timeoutActive && ScoreActions::isMedicalActive();

  if (medicalActive) {
    uint32_t remaining = ScoreActions::medicalCountdownMs();
    uint32_t now       = millis();
    if (remaining > 0 && now - lastMedicalUpdate >= 33) {
      lastMedicalUpdate = now;
      xSemaphoreTake(scoreMutex, portMAX_DELAY);
      LED::showMedicalTimer(remaining);
      xSemaphoreGive(scoreMutex);
    }
  } else if (prevMedicalActive && !timeoutActive) {
    xSemaphoreTake(scoreMutex, portMAX_DELAY);
    LED::update(currentScore);
    xSemaphoreGive(scoreMutex);
  }
  prevMedicalActive = medicalActive;

  static bool     prevTimerActive = false;
  static uint32_t lastTimerUpdate = 0;
  uint32_t timerMs    = ScoreActions::breakTimerRemainingMs();
  bool     timerActive = !timeoutActive && !medicalActive && (timerMs > 0);

  if (timerActive) {
    uint32_t now = millis();
    if (now - lastTimerUpdate >= 250) {
      lastTimerUpdate = now;
      xSemaphoreTake(scoreMutex, portMAX_DELAY);
      LED::showBreakTimer(timerMs, true);
      xSemaphoreGive(scoreMutex);
    }
  } else if (prevTimerActive && !timeoutActive && !medicalActive) {
    xSemaphoreTake(scoreMutex, portMAX_DELAY);
    LED::update(currentScore);
    xSemaphoreGive(scoreMutex);
  }
  prevTimerActive = timerActive;

  static bool     prevIntroActive     = false;
  static uint32_t lastIntroUpdate     = 0;
  static uint8_t  lastFirstServer     = 0;
  static uint32_t introResumeAt       = 0;
  static constexpr uint32_t INTRO_RESUME_DELAY_MS = 4000; // let the serve-change be seen before resuming

  xSemaphoreTake(scoreMutex, portMAX_DELAY);
  bool matchNotStarted = (currentScore.scoreA + currentScore.scoreB +
                           currentScore.setA   + currentScore.setB) == 0;
  uint8_t curFirstServer = currentScore.firstServer;
  xSemaphoreGive(scoreMutex);

  // Whenever "first server" changes (portal tap, pedal a/long|b/long, or a
  // central "set_serving" command) while pre-match, hold off the marquee for
  // a few seconds so the operator can actually see the serve indicator change.
  // A server picked from somewhere else also answers a pending "SERVE?" prompt.
  if (curFirstServer != lastFirstServer) {
    lastFirstServer = curFirstServer;
    introResumeAt    = millis() + INTRO_RESUME_DELAY_MS;
    ScoreActions::cancelServerSelect();
  }

  // The marquee stays dismissed (after a server was picked from the "SERVE?"
  // prompt) until a new match shows up: a reset (handled in apply()), new
  // team names, or the score going back to fresh after a completed set.
  static TeamNames::Names lastNames = TeamNames::get();
  const TeamNames::Names& curNames = TeamNames::get();
  if (strcmp(curNames.teamA, lastNames.teamA) != 0 || strcmp(curNames.teamB, lastNames.teamB) != 0) {
    lastNames = curNames;
    ScoreActions::clearIntroDismissed();
  }
  if (!matchNotStarted) {
    ScoreActions::cancelServerSelect();
    xSemaphoreTake(scoreMutex, portMAX_DELAY);
    bool anySetPlayed = (currentScore.setA + currentScore.setB) > 0;
    xSemaphoreGive(scoreMutex);
    if (anySetPlayed) ScoreActions::clearIntroDismissed();
  }

  ScoreActions::tickServerSelect();
  static bool prevServerSelect = false;
  bool serverSelect = !timeoutActive && !medicalActive && !timerActive && ScoreActions::isServerSelectActive();
  if (serverSelect) {
    static uint32_t lastPromptUpdate = 0;
    uint32_t now = millis();
    if (now - lastPromptUpdate >= 33) {
      lastPromptUpdate = now;
      xSemaphoreTake(scoreMutex, portMAX_DELAY);
      LED::showServerPrompt();
      xSemaphoreGive(scoreMutex);
    }
  } else if (prevServerSelect) {
    xSemaphoreTake(scoreMutex, portMAX_DELAY);
    LED::update(currentScore);
    xSemaphoreGive(scoreMutex);
  }
  prevServerSelect = serverSelect;

  bool introActive = !timeoutActive && !medicalActive && !timerActive && !serverSelect &&
                      !ScoreActions::isIntroDismissed() && TeamNames::hasAnyTeamName() &&
                      matchNotStarted && (int32_t)(millis() - introResumeAt) >= 0;
  ScoreActions::setIntroShowing(introActive);

  if (introActive) {
    uint32_t now = millis();
    if (now - lastIntroUpdate >= 33) {
      lastIntroUpdate = now;
      xSemaphoreTake(scoreMutex, portMAX_DELAY);
      LED::showTeamIntro(TeamNames::get());
      xSemaphoreGive(scoreMutex);
    }
  } else if (prevIntroActive && !serverSelect) {
    xSemaphoreTake(scoreMutex, portMAX_DELAY);
    LED::update(currentScore);
    xSemaphoreGive(scoreMutex);
  }
  prevIntroActive = introActive;

  static uint32_t lastLog = 0;
  if (millis() - lastLog > 10000) {
    lastLog = millis();
    Serial.printf("[WIFI] Mode: %s | AP: %s | STA: %s | Heap: %d\n",
      Mode::_modeName(Mode::get()),
      WiFi.softAPIP().toString().c_str(),
      WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "disconnected",
      ESP.getFreeHeap()
    );
  }

  delay(10);
}
