/**
 * Roundnet Scoreboard
 */

#include <Arduino.h>
#include "config.h"
#include "mode.h"
#include "score.h"
#include "led.h"
#include "wifi_mgr.h"
#include "firebase.h"
#include "portal.h"
#include "espnow_handler.h"

Score currentScore;
SemaphoreHandle_t scoreMutex = NULL;
TaskHandle_t firebaseTaskHandle = nullptr;

// ── Firebase task (Core 0, Firebase mode only) ────────────────────────────────

void firebaseTask(void*) {
  const uint32_t INTERVAL_ERR = 15000;
  const uint8_t  MAX_ERRORS   = 5;

  uint32_t lastRead          = 0;
  uint8_t  consecutiveErrors = 0;

  Score   lastWritten   = {};
  uint8_t lastWrittenWP = 255;  // force first write of winPoints
  uint8_t lastWrittenHC = 255;  // force first write of hardcap
  uint8_t lastWrittenFS = 255;

  for (;;) {
    WiFiMgr::tick();

    if (WiFiMgr::isOnline()) {
      xSemaphoreTake(scoreMutex, portMAX_DELAY);
      Score local = currentScore;
      xSemaphoreGive(scoreMutex);

      // Push local changes to Firebase
      if (local.scoreA != lastWritten.scoreA || local.scoreB != lastWritten.scoreB ||
          local.setA   != lastWritten.setA   || local.setB   != lastWritten.setB) {
        if (Firebase::writeScore(local)) {
          lastWritten = local;
          lastRead = millis();
        }
      }
      if (local.winPoints != lastWrittenWP) {
        if (Firebase::writeWinPoints(local.winPoints)) lastWrittenWP = local.winPoints;
      }
      if (local.hardcap != lastWrittenHC) {
        if (Firebase::writeHardcap(local.hardcap)) lastWrittenHC = local.hardcap;
      }
      if (local.firstServer != lastWrittenFS) {
        if (Firebase::writeFirstServer(local)) lastWrittenFS = local.firstServer;
      }

      // Too many errors: pause 30s
      if (consecutiveErrors >= MAX_ERRORS) {
        Serial.println("[Firebase] Too many errors — pausing 30s");
        vTaskDelay(30000 / portTICK_PERIOD_MS);
        consecutiveErrors = 0;
        continue;
      }

      // Periodically read Firebase for external changes
      uint32_t interval = consecutiveErrors > 0 ? INTERVAL_ERR : Firebase::getPollIntervalMs();
      uint32_t now = millis();
      if ((now - lastRead) >= interval) {
        lastRead = now;
        Score db = local;
        if (Firebase::readScore(db)) {
          consecutiveErrors = 0;
          if (db.scoreA      != lastWritten.scoreA     || db.scoreB     != lastWritten.scoreB   ||
              db.setA        != lastWritten.setA        || db.setB       != lastWritten.setB     ||
              db.firstServer != lastWritten.firstServer || db.winPoints  != lastWritten.winPoints ||
              db.hardcap     != lastWritten.hardcap) {

            // Detect set change (active set number increased)
            bool setJustEnded = (db.setA + db.setB) > (lastWritten.setA + lastWritten.setB);

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

            lastWritten   = db;
            lastWrittenWP = db.winPoints;
            lastWrittenHC = db.hardcap;
            lastWrittenFS = db.firstServer;
          }
        } else {
          consecutiveErrors++;
          Serial.printf("[Firebase] Error %d/%d\n", consecutiveErrors, MAX_ERRORS);
        }
      }
    } else {
      consecutiveErrors = 0;
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

  static bool     _rotWaiting   = false;
  static uint32_t _rotAnimStart = 0;
  if (!_rotWaiting && ScoreActions::getAndClearRotation()) {
    _rotWaiting   = true;
    _rotAnimStart = millis();
  }
  if (_rotWaiting && (millis() - _rotAnimStart) >= 1500) {
    _rotWaiting = false;
    LED::rotationAnimation();
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

  static bool     prevTimerActive = false;
  static uint32_t lastTimerUpdate = 0;
  uint32_t timerMs    = ScoreActions::breakTimerRemainingMs();
  bool     timerActive = !timeoutActive && (timerMs > 0);

  if (timerActive) {
    uint32_t now = millis();
    if (now - lastTimerUpdate >= 250) {
      lastTimerUpdate = now;
      xSemaphoreTake(scoreMutex, portMAX_DELAY);
      LED::showBreakTimer(timerMs, true);
      xSemaphoreGive(scoreMutex);
    }
  } else if (prevTimerActive && !timeoutActive) {
    xSemaphoreTake(scoreMutex, portMAX_DELAY);
    LED::update(currentScore);
    xSemaphoreGive(scoreMutex);
  }
  prevTimerActive = timerActive;

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
