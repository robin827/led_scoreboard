/**
 * config.h - Configuration minimale scoreboard
 */

#pragma once

// ── WiFi Hotspot ──────────────────────────────────────────────────────────
#define AP_SSID       "RN-Score"
#define AP_PASSWORD   "roundnet"

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