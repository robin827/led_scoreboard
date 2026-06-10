/**
 * Roundnet Scoreboard
 */

#include <Arduino.h>
#include "config.h"
#include "score.h"
#include "led.h"
#include "wifi_mgr.h"
#include "portal.h"
#include "espnow_handler.h"

// Instance globale du score
Score currentScore;
SemaphoreHandle_t scoreMutex = NULL;

void setup() {
  scoreMutex = xSemaphoreCreateMutex();

  // LEDs first: boot animation plays while USB CDC enumerates
  LED::init();
  LED::bootAnimation();

  Serial.begin(115200);
  delay(200);

  Serial.println("\n=== ROUNDNET SCOREBOARD ===");
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());

  // 1. Battery saver
  ScoreActions::initBatterySaver();

  // 2. WiFi (AP + tentative STA)
  Serial.println("[1/3] Init WiFi...");
  WiFiMgr::init();

  // 3. ESP-NOW (requires WiFi up)
  Serial.println("[2/3] Init ESP-NOW...");
  EspNow::init();

  // 4. Portal + WebSocket client
  Serial.println("[3/3] Init Portal...");
  Portal::init();
  WsClient::init(WsClient::loadServerIp());

  LED::update(currentScore);

  Serial.println("\n=== READY ===");
  Serial.printf("AP: %s | Portal: http://%s\n", WiFiMgr::getScoreboardId().c_str(), WiFiMgr::apIP().c_str());
}

void loop() {
  WsClient::tick();
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
      LED::showSleepAnimation(ScoreActions::getPreSleepBrightness());
      xSemaphoreGive(scoreMutex);
    }
    delay(10);
    return;
  }

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
