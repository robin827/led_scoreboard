/**
 * config.h - Configuration minimale scoreboard
 */

#pragma once

// ── Firmware version & OTA manifest ──────────────────────────────────────────
// Bump FIRMWARE_VERSION on every release; publish a matching manifest JSON at
// MANIFEST_URL with the fields: { "version", "url", "notes" }
#define FIRMWARE_VERSION  "1.0.2"
#define MANIFEST_URL      "https://api.github.com/repos/robin827/led_scoreboard/releases/latest"

// ── Scoreboard identity (used for AP name, ESP-NOW beacon, etc.) ──────────
#define SCOREBOARD_ID "Roundnet Scoreboard"

// ── WiFi Hotspot ──────────────────────────────────────────────────────────
#define AP_SSID       SCOREBOARD_ID
#define AP_PASSWORD   ""

// ── Firebase Realtime Database ────────────────────────────────────────────
#define FIREBASE_DATABASE_URL     "https://live-scoreboard-fc0e5-default-rtdb.europe-west1.firebasedatabase.app"
#define FIREBASE_DATABASE_SECRET  "ta-database-secret"  // Pas utilisé si règles publiques

// ── LED Panel ─────────────────────────────────────────────────────────────
#define LED_PIN       13
#define LED_BRIGHTNESS 10

namespace Config {
  static constexpr uint8_t  PIN           = LED_PIN;
  static constexpr uint8_t  NUM_ROWS      = 8;
  static constexpr uint8_t  NUM_COLS      = 24;  // Matrice 24x8
  static constexpr uint16_t NUM_LEDS      = NUM_ROWS * NUM_COLS;  // 192
  static constexpr uint8_t  BRIGHTNESS    = LED_BRIGHTNESS;
  static constexpr uint32_t READ_INTERVAL = 1000;
}