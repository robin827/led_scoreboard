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

// Instance globale du score
Score currentScore;

// Tâche Firebase sur Core 0
TaskHandle_t firebaseTaskHandle = NULL;

void firebaseTask(void* parameter) {
  uint32_t lastRead = 0;
  
  for(;;) {
    if (Mode::isRead() && WiFiMgr::isOnline()) {
      uint32_t now = millis();
      if ((now - lastRead) >= Config::READ_INTERVAL) {
        lastRead = now;
        
        Score newScore;
        if (Firebase::readScore(newScore)) {
          if (newScore.scoreA != currentScore.scoreA ||
              newScore.scoreB != currentScore.scoreB ||
              newScore.setA != currentScore.setA ||
              newScore.setB != currentScore.setB) {
            currentScore = newScore;
            LED::update(currentScore);
          }
        }
      }
    }
    
    vTaskDelay(100 / portTICK_PERIOD_MS);  // Sleep 100ms
  }
}

void setup() {
  delay(2000);
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n=== ROUNDNET SCOREBOARD ===");
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  
  // 1. Mode
  Serial.println("[1/4] Init Mode...");
  Mode::init();
  
  // 2. WiFi (AP + tentative STA)
  Serial.println("[2/4] Init WiFi...");
  WiFiMgr::init();
  
  // 3. Portail captif
  Serial.println("[3/4] Init Portal...");
  Portal::init();
  
  // 4. LEDs
  Serial.println("[4/5] Init LEDs...");
  LED::init();
  LED::update(currentScore);
  
  // 5. Tâche Firebase sur Core 0
  Serial.println("[5/5] Creating Firebase task on Core 0...");
  xTaskCreatePinnedToCore(
    firebaseTask,         // Fonction
    "FirebaseTask",       // Nom
    8192,                 // Stack size
    NULL,                 // Paramètres
    1,                    // Priorité
    &firebaseTaskHandle,  // Handle
    0                     // Core 0
  );
  
  Serial.println("\n=== READY ===");
  Serial.printf("AP: %s | Portal: http://%s\n", AP_SSID, WiFiMgr::apIP().c_str());
}

void loop() {
  // Core 1 (défaut) : Portail HTTP + WiFi management
  WiFiMgr::tick();
  Portal::tick();
  
  delay(10);
}
// Core 0 gère Firebase en parallèle via firebaseTask()