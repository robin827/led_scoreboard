/**
 * Roundnet Scoreboard - Version MINIMALE
 * WiFi AP + Portail captif + Score + LEDs
 * Rien d'autre.
 */

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "score.h"
#include "led.h"
#include "portal.h"

// Instance globale du score
Score currentScore;

void setup() {
  delay(2000);
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n=== ROUNDNET SCOREBOARD (minimal) ===");
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  
  // 1. WiFi AP
  Serial.println("[1/3] Starting WiFi AP...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  delay(500);
  Serial.printf("[1/3] ✓ AP: %s | IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  
  // 2. Portail captif
  Serial.println("[2/3] Starting Portal...");
  Portal::init();
  Serial.println("[2/3] ✓ Portal OK");
  
  // 3. LEDs
  Serial.println("[3/3] Starting LEDs...");
  LED::init();
  LED::update(currentScore);
  Serial.println("[3/3] ✓ LEDs OK");
  
  Serial.println("\n=== READY ===");
  Serial.printf("Connect to: %s (password: %s)\n", AP_SSID, AP_PASSWORD);
  Serial.printf("Then open: http://%s\n\n", WiFi.softAPIP().toString().c_str());
}

void loop() {
  Portal::tick();
  delay(10);
}
