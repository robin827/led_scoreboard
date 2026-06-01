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
  const uint32_t READ_INTERVAL_SUCCESS = Config::READ_INTERVAL;
  const uint32_t READ_INTERVAL_ERROR = 15000;   // 15s si erreur
  uint32_t lastRead = 0;
  uint32_t currentInterval = READ_INTERVAL_SUCCESS;
  uint8_t consecutiveErrors = 0;
  const uint8_t MAX_CONSECUTIVE_ERRORS = 5;
  
  for(;;) {
    WiFiMgr::tick();  // STA connection management on Core 0

    if (Mode::isRead() && WiFiMgr::isOnline()) {
      uint32_t now = millis();
      if ((now - lastRead) >= currentInterval) {
        lastRead = now;
        
        // Pause si trop d'erreurs consécutives
        if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
          Serial.println("[Firebase] Too many errors, pausing for 30s");
          vTaskDelay(30000 / portTICK_PERIOD_MS);
          consecutiveErrors = 0;
          continue;
        }
        
        Serial.printf("[Firebase] Free heap before: %d\n", ESP.getFreeHeap());
        
        Score newScore = currentScore;
        if (Firebase::readScore(newScore)) {
          consecutiveErrors = 0;  // Reset sur succès
          currentInterval = READ_INTERVAL_SUCCESS;
          
          if (newScore.scoreA      != currentScore.scoreA   ||
              newScore.scoreB      != currentScore.scoreB   ||
              newScore.setA        != currentScore.setA     ||
              newScore.setB        != currentScore.setB     ||
              newScore.firstServer != currentScore.firstServer ||
              newScore.winPoints   != currentScore.winPoints) {
            xSemaphoreTake(scoreMutex, portMAX_DELAY);
            currentScore = newScore;
            LED::update(currentScore);
            xSemaphoreGive(scoreMutex);
          }
        } else {
          consecutiveErrors++;
          currentInterval = READ_INTERVAL_ERROR;  // Ralentit sur erreur
          Serial.printf("[Firebase] Error %d/%d, slowing down\n", 
            consecutiveErrors, MAX_CONSECUTIVE_ERRORS);
        }
        
        Serial.printf("[Firebase] Free heap after: %d\n", ESP.getFreeHeap());
      }
    } else if (Mode::isWrite() && WiFiMgr::isOnline()) {
      // Write mode: push score and win_points to Firebase whenever they change
      static Score   lastWritten;
      static uint8_t lastWrittenWP  = 255;  // force first write
      static uint8_t lastWrittenFS  = 255;  // force first write
      xSemaphoreTake(scoreMutex, portMAX_DELAY);
      Score toWrite = currentScore;
      xSemaphoreGive(scoreMutex);

      if (toWrite.scoreA != lastWritten.scoreA ||
          toWrite.scoreB != lastWritten.scoreB ||
          toWrite.setA   != lastWritten.setA   ||
          toWrite.setB   != lastWritten.setB) {
        if (Firebase::writeScore(toWrite)) {
          lastWritten = toWrite;
        }
      }

      if (toWrite.winPoints != lastWrittenWP) {
        if (Firebase::writeWinPoints(toWrite.winPoints)) {
          lastWrittenWP = toWrite.winPoints;
        }
      }

      if (toWrite.firstServer != lastWrittenFS) {
        if (Firebase::writeFirstServer(toWrite)) {
          lastWrittenFS = toWrite.firstServer;
        }
      }
    } else {
      // LOCAL mode or offline — reset READ counters
      consecutiveErrors = 0;
      currentInterval = READ_INTERVAL_SUCCESS;
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

  // Break timer display (between sets)
  static bool     prevTimerActive = false;
  static uint32_t lastTimerUpdate = 0;
  uint32_t timerMs    = ScoreActions::breakTimerRemainingMs();
  bool     timerActive = (timerMs > 0);

  if (timerActive) {
    uint32_t now = millis();
    if (now - lastTimerUpdate >= 250) {
      lastTimerUpdate = now;
      xSemaphoreTake(scoreMutex, portMAX_DELAY);
      LED::showBreakTimer(timerMs, true);
      xSemaphoreGive(scoreMutex);
    }
  } else if (prevTimerActive) {
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
