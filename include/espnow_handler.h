#pragma once
#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "score_actions.h"
#include "config.h"
#include "wifi_mgr.h"

namespace EspNow {

static uint32_t _lastBeacon = 0;
static constexpr uint32_t BEACON_INTERVAL_MS = 150;

static void _onReceive(const uint8_t* mac, const uint8_t* data, int len) {
  Serial.printf("[ESP-NOW] RX from %02X:%02X:%02X:%02X:%02X:%02X len=%d\n",
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], len);

  // Raw hex dump
  Serial.print("[ESP-NOW] raw: ");
  for (int i = 0; i < len; i++) Serial.printf("%02X ", data[i]);
  Serial.println();

  if (len < 1 || len > 16) {
    Serial.printf("[ESP-NOW] ignored: len=%d out of range\n", len);
    return;
  }
  char cmd[17];
  memcpy(cmd, data, len);
  cmd[len] = '\0';
  // Ignore beacon strings — our own broadcast or a nearby scoreboard
  if (strncmp(cmd, "SCORE:", 6) == 0) {
    Serial.printf("[ESP-NOW] ignored beacon: '%s'\n", cmd);
    return;
  }
  Serial.printf("[ESP-NOW] cmd='%s'\n", cmd);
  bool ok = ScoreActions::apply(cmd);
  Serial.printf("[ESP-NOW] apply result: ok=%d\n", ok);
  if (ok) WsClient::requestPush();
}

inline void init() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init failed");
    return;
  }
  esp_now_register_recv_cb(_onReceive);

  // Register broadcast peer so we can send beacon packets
  esp_now_peer_info_t peer = {};
  memset(peer.peer_addr, 0xFF, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  Serial.println("[ESP-NOW] Ready");
}

// Call from loop() — broadcasts presence every BEACON_INTERVAL_MS
inline void tick() {
  uint32_t now = millis();
  if (now - _lastBeacon < BEACON_INTERVAL_MS) return;
  _lastBeacon = now;

  // Payload: "SCORE:<name>" — sender uses prefix to identify scoreboards
  char beacon[40];  // "SCORE:" (6) + up to 31 chars + null
  snprintf(beacon, sizeof(beacon), "SCORE:%s", WiFiMgr::getScoreboardId().c_str());
  static const uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  esp_now_send(broadcast, (const uint8_t*)beacon, strlen(beacon));
}

} // namespace EspNow
