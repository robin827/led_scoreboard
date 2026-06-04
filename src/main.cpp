/**
 * Roundnet Scoreboard - Version avec mode LOCAL / READ
 */

#include <Arduino.h>
#include "config.h"
#include "score.h"
#include "led.h"
#include "mode.h"
#include "wifi_mgr.h"
#include "firebase.h"
#include "portal.h"
#include "espnow_handler.h"

// Instance globale du score
Score currentScore;
SemaphoreHandle_t scoreMutex = NULL;

// Tâche Firebase sur Core 0
TaskHandle_t firebaseTaskHandle = NULL;

void firebaseTask(void* parameter) {
  const uint32_t INTERVAL_OK  = Config::READ_INTERVAL;
  const uint32_t INTERVAL_ERR = 15000;
  uint32_t lastRead          = 0;
  uint32_t currentInterval   = INTERVAL_OK;
  uint8_t  consecutiveErrors = 0;
  const uint8_t MAX_ERRORS   = 5;

  // Track what we last successfully pushed so we can detect external changes on read
  Score   lastWritten;
  uint8_t lastWrittenWP = 255;  // 255 forces first write
  uint8_t lastWrittenFS = 255;

  for (;;) {
    WiFiMgr::tick();

    if (Mode::isSync() && WiFiMgr::isOnline()) {
      xSemaphoreTake(scoreMutex, portMAX_DELAY);
      Score local = currentScore;
      xSemaphoreGive(scoreMutex);

      // ── Push local changes to Firebase ───────────────────────────────────
      if (local.scoreA != lastWritten.scoreA || local.scoreB != lastWritten.scoreB ||
          local.setA   != lastWritten.setA   || local.setB   != lastWritten.setB) {
        if (Firebase::writeScore(local)) {
          lastWritten = local;
          lastRead = millis();  // delay next read — give Firebase time to propagate
        }
      }
      if (local.winPoints != lastWrittenWP) {
        if (Firebase::writeWinPoints(local.winPoints)) lastWrittenWP = local.winPoints;
      }
      if (local.firstServer != lastWrittenFS) {
        if (Firebase::writeFirstServer(local)) lastWrittenFS = local.firstServer;
      }

      // ── Periodically read Firebase for external changes ───────────────────
      uint32_t now = millis();
      if ((now - lastRead) >= currentInterval) {
        lastRead = now;

        if (consecutiveErrors >= MAX_ERRORS) {
          Serial.println("[Firebase] Too many errors, pausing 30s");
          vTaskDelay(30000 / portTICK_PERIOD_MS);
          consecutiveErrors = 0;
          continue;
        }

        Score db = local;
        if (Firebase::readScore(db)) {
          consecutiveErrors = 0;
          currentInterval   = INTERVAL_OK;

          // Apply only if DB differs from what we last wrote — means external source changed it
          if (db.scoreA != lastWritten.scoreA || db.scoreB != lastWritten.scoreB ||
              db.setA   != lastWritten.setA   || db.setB   != lastWritten.setB   ||
              db.firstServer != lastWritten.firstServer ||
              db.winPoints   != lastWritten.winPoints) {
            ScoreActions::applyFromDatabase(db);
            lastWritten   = db;  // now in sync with Firebase
            lastWrittenWP = db.winPoints;
            lastWrittenFS = db.firstServer;
          }
        } else {
          consecutiveErrors++;
          currentInterval = INTERVAL_ERR;
          Serial.printf("[Firebase] Error %d/%d\n", consecutiveErrors, MAX_ERRORS);
        }
      }
    } else {
      // LOCAL or offline — reset error counters
      consecutiveErrors = 0;
      currentInterval   = INTERVAL_OK;
    }

    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void setup() {
  scoreMutex = xSemaphoreCreateMutex();

  // LEDs first: boot animation plays while USB CDC enumerates
  LED::init();
  LED::bootAnimation();

  Serial.begin(115200);
  delay(200);

  Serial.println("\n=== ROUNDNET SCOREBOARD ===");
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());

  // 1. Mode
  Serial.println("[1/5] Init Mode...");
  Mode::init();
  ScoreActions::initBatterySaver();

  // 2. WiFi (AP + tentative STA)
  Serial.println("[2/5] Init WiFi...");
  WiFiMgr::init();

  // 3. ESP-NOW (requires WiFi up)
  Serial.println("[3/5] Init ESP-NOW...");
  EspNow::init();

  // 4. Portail captif
  Serial.println("[4/5] Init Portal...");
  Portal::init();

  LED::update(currentScore);

  // 5. Tâche Firebase sur Core 0
  Serial.println("[5/5] Creating Firebase task on Core 0...");
  xTaskCreatePinnedToCore(
    firebaseTask,
    "FirebaseTask",
    8192,
    NULL,
    1,
    &firebaseTaskHandle,
    0
  );

  Serial.println("\n=== READY ===");
  Serial.printf("AP: %s | Portal: http://%s\n", WiFiMgr::getScoreboardId().c_str(), WiFiMgr::apIP().c_str());
}

void loop() {
  Portal::tick();
  EspNow::tick();
  ScoreActions::tickBatterySaver();

  // Rotation animation: non-blocking 1.5 s wait so portal stays responsive,
  // then 2 s spinning blue comets (blocking is fine at that point — banner already shown)
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

  // Timeout display: 60 s countdown with breathing "TIMEOUT" label (~30 fps for breathing)
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

  // Break timer display (between sets) — only when timeout is not active
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
    Serial.printf("[WIFI] Mode: %d | AP IP: %s | STA: %s | Free heap: %d\n",
      WiFi.getMode(),
      WiFi.softAPIP().toString().c_str(),
      WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "disconnected",
      ESP.getFreeHeap()
    );
  }

  delay(10);
}
// Core 0 gère Firebase en parallèle via firebaseTask()
