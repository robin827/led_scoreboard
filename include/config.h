/**
 * config.h - Configuration minimale scoreboard
 */

#pragma once

// ── WiFi Hotspot ──────────────────────────────────────────────────────────
#define AP_SSID       "RN-Score"
#define AP_PASSWORD   "roundnet"

// ── LED Panel ─────────────────────────────────────────────────────────────
#define LED_PIN       13
#define LED_BRIGHTNESS 80

namespace Config {
  static constexpr uint8_t  PIN           = LED_PIN;
  static constexpr uint8_t  NUM_ROWS      = 8;
  static constexpr uint8_t  NUM_COLS      = 24;
  static constexpr uint16_t NUM_LEDS      = NUM_ROWS * NUM_COLS;  // 192
  static constexpr uint8_t  BRIGHTNESS    = LED_BRIGHTNESS;
}
