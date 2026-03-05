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
        
        Score newScore;
        if (Firebase::readScore(newScore)) {
          consecutiveErrors = 0;  // Reset sur succès
          currentInterval = READ_INTERVAL_SUCCESS;
          
          if (newScore.scoreA != currentScore.scoreA ||
              newScore.scoreB != currentScore.scoreB ||
              newScore.setA != currentScore.setA ||
              newScore.setB != currentScore.setB) {
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
    } else {
      // Reset compteur d'erreurs en mode LOCAL
      consecutiveErrors = 0;
      currentInterval = READ_INTERVAL_SUCCESS;
    }
    
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void setup() {
  delay(2000);
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n=== ROUNDNET SCOREBOARD ===");
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());

  scoreMutex = xSemaphoreCreateMutex();

  // 1. Mode
  Serial.println("[1/4] Init Mode...");
  Mode::init();
  
  // 2. WiFi (AP + tentative STA)
  Serial.println("[2/4] Init WiFi...");
  WiFiMgr::init();
  
  // 3. Portail captif
  Serial.println("[3/5] Init Portal...");
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
  // Core 1 (défaut) : Portail HTTP uniquement
  Portal::tick();
  
  // Log périodique de l'état WiFi (toutes les 10s)
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
