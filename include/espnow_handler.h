#pragma once
#include <Arduino.h>
#include <esp_now.h>
#include "score_actions.h"

namespace EspNow {

static void _onReceive(const uint8_t* mac, const uint8_t* data, int len) {
  if (len < 1 || len > 16) return;
  char cmd[17];
  memcpy(cmd, data, len);
  cmd[len] = '\0';
  bool ok = ScoreActions::apply(cmd);
  Serial.printf("[ESP-NOW] cmd='%s' ok=%d\n", cmd, ok);
}

inline void init() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init failed");
    return;
  }
  esp_now_register_recv_cb(_onReceive);
  Serial.println("[ESP-NOW] Ready");
}

} // namespace EspNow
