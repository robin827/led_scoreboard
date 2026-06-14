/**
 * ws_client.h - WebSocket central server client
 *
 * Connects to a WebSocket server when WiFi is available.
 * Pushes full state on connect and on any score/brightness change.
 * Receives JSON commands from the server.
 * Non-blocking — call tick() from loop().
 *
 * Note: connect() in ArduinoWebsockets is synchronous. If the server IP is
 * unreachable, _tryConnect() may stall loop() for a few seconds every 5 s.
 * This is acceptable on a local network with a reachable server.
 */

#pragma once
#include <Arduino.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Preferences.h>
#include "score.h"
#include "led.h"
#include "score_actions.h"
#include "wifi_mgr.h"

extern Score currentScore;
extern SemaphoreHandle_t scoreMutex;

namespace WsClient {

static websockets::WebsocketsClient _ws;
static String        _serverIp;
static uint16_t      _serverPort  = 8080;
static bool          _connected   = false;
static bool          _initialized = false;
static unsigned long _lastAttempt = 0;
static bool          _wifiWas          = false;
static bool          _wifiEverConnected = false;
static volatile bool _needsPush   = false;
static unsigned long _lastPush    = 0;

static constexpr unsigned long RECONNECT_MS  = 5000;
static constexpr unsigned long RECONNECT_MAX = 300000; // 5 min cap after repeated failures
static constexpr unsigned long HEARTBEAT_MS  = 3000;
static unsigned int      _failCount     = 0;
static unsigned long     _nextReconnect = RECONNECT_MS;
static volatile bool     _connecting    = false;

// ── NVS ──────────────────────────────────────────────────────────────────────

inline String loadServerIp() {
  Preferences prefs;
  prefs.begin("wsconfig", true);
  String ip = prefs.getString("serverIp", "");
  prefs.end();
  return ip;
}

inline void saveServerIp(const String& ip) {
  Preferences prefs;
  prefs.begin("wsconfig", false);
  prefs.putString("serverIp", ip);
  prefs.end();
}

inline String getServerIp() { return _serverIp; }
inline bool   isConnected()  { return _connected; }

// ── State push ───────────────────────────────────────────────────────────────

inline void pushState() {
  if (!_connected) return;

  xSemaphoreTake(scoreMutex, portMAX_DELAY);
  Score s = currentScore;
  xSemaphoreGive(scoreMutex);

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  JsonDocument doc;
  doc["type"]      = "state";
  doc["id"]        = macStr;
  JsonArray scores = doc["scores"].to<JsonArray>();
  scores.add(s.scoreA);
  scores.add(s.scoreB);
  ServeInfo srv    = getServeInfo(s);
  doc["serving"]   = srv.teamAServing ? "A" : "B";
  doc["firstServer"] = s.firstServer;
  doc["servesLeft"]  = srv.servesLeft;
  doc["serveTotal"]  = srv.serveTotal;
  doc["win_score"] = s.winPoints;
  doc["brightness"]= LED::getBrightness();
  doc["uptime"]    = (unsigned long)(esp_timer_get_time() / 1000000ULL);
  doc["mode"]      = "central";
  doc["hardcap"]   = s.hardcap;
  doc["setA"]      = s.setA;
  doc["setB"]      = s.setB;
  doc["timeoutMs"] = ScoreActions::timeoutCountdownMs();
  doc["breakMs"]   = ScoreActions::breakTimerRemainingMs();

  uint8_t setsPlayed = s.setA + s.setB;
  JsonArray ha = doc["histA"].to<JsonArray>();
  JsonArray hb = doc["histB"].to<JsonArray>();
  for (uint8_t i = 0; i < setsPlayed && i < 3; i++) {
    ha.add(s.histA[i]);
    hb.add(s.histB[i]);
  }

  doc["boardId"] = WiFiMgr::getScoreboardId();
  doc["pedals"].to<JsonArray>();

  String json;
  serializeJson(doc, json);
  _ws.send(json);
}

inline void requestPush() { _needsPush = true; }

// ── Command handler ───────────────────────────────────────────────────────────

static void _handleCommand(const String& payload) {
  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) return;
  if (doc["type"] != "command") return;

  const char* action = doc["action"] | "";
  int         value  = doc["value"]  | 0;
  const char* team   = doc["team"]   | "";
  int         delta  = doc["delta"]  | 0;
  int         tA     = doc["teamA"]  | -1;
  int         tB     = doc["teamB"]  | -1;

  if (strcmp(action, "increment_score") == 0) {
    if      (strcmp(team, "A") == 0) ScoreActions::apply(delta >= 0 ? "a/inc" : "a/dec");
    else if (strcmp(team, "B") == 0) ScoreActions::apply(delta >= 0 ? "b/inc" : "b/dec");
    pushState();
  } else if (strcmp(action, "reset_score") == 0) {
    ScoreActions::apply("reset");
    pushState();
  } else if (strcmp(action, "set_serving") == 0) {
    ScoreActions::notifyActivity();
    xSemaphoreTake(scoreMutex, portMAX_DELAY);
    currentScore.firstServer = (strcmp(team, "A") == 0) ? 0 : 1;
    LED::update(currentScore);
    xSemaphoreGive(scoreMutex);
    pushState();
  } else if (strcmp(action, "set_score") == 0) {
    ScoreActions::notifyActivity();
    xSemaphoreTake(scoreMutex, portMAX_DELAY);
    if (tA >= 0) currentScore.scoreA = (uint8_t)constrain(tA, 0, 99);
    if (tB >= 0) currentScore.scoreB = (uint8_t)constrain(tB, 0, 99);
    LED::update(currentScore);
    xSemaphoreGive(scoreMutex);
    pushState();
  } else if (strcmp(action, "set_win_score") == 0) {
    ScoreActions::notifyActivity();
    xSemaphoreTake(scoreMutex, portMAX_DELAY);
    if (value >= 5 && value <= 99) currentScore.winPoints = (uint8_t)value;
    LED::update(currentScore);
    xSemaphoreGive(scoreMutex);
    pushState();
  } else if (strcmp(action, "set_brightness") == 0) {
    ScoreActions::notifyActivity();
    LED::setBrightness((uint8_t)constrain(value, 1, 255));
    xSemaphoreTake(scoreMutex, portMAX_DELAY);
    LED::update(currentScore);
    xSemaphoreGive(scoreMutex);
    pushState();
  } else if (strcmp(action, "next_set") == 0) {
    ScoreActions::apply("nextset");
    pushState();
  } else if (strcmp(action, "timeout") == 0) {
    ScoreActions::apply("timeout");
    pushState();
  } else if (strcmp(action, "set_hardcap") == 0) {
    ScoreActions::notifyActivity();
    xSemaphoreTake(scoreMutex, portMAX_DELAY);
    currentScore.hardcap = (uint8_t)constrain(value, 0, 99);
    LED::update(currentScore);
    xSemaphoreGive(scoreMutex);
    pushState();
  } else if (strcmp(action, "sleep") == 0) {
    ScoreActions::activateSleep();
  }
}

// ── Connection ────────────────────────────────────────────────────────────────

static void _tryConnect() {
  String url = "ws://" + _serverIp + ":" + String(_serverPort);
  Serial.printf("[WS] Connecting to %s\n", url.c_str());

  _ws.connect(url);

  if (_connected) {
    _failCount     = 0;
    _nextReconnect = RECONNECT_MS;
  } else {
    _failCount++;
    _nextReconnect = min(_nextReconnect * 2UL, (unsigned long)RECONNECT_MAX);
    Serial.printf("[WS] Failed (attempt %u), retry in %lus\n", _failCount, _nextReconnect / 1000);
  }
  _connecting = false;
}

static void _connectTaskFn(void*) {
  _tryConnect();
  vTaskDelete(nullptr); // self-deletes when done
}

// ── Public API ────────────────────────────────────────────────────────────────

inline void stop() {
  if (_connected) {
    _ws.close();
    _connected = false;
  }
  _initialized = false;
  _connecting  = false;
  _wifiWas     = false;
  Serial.println("[WS] Stopped");
}

inline void init(const String& serverIp, uint16_t port = 8080) {
  _serverIp    = serverIp;
  _serverPort  = port;
  _initialized = true;

  _ws.onMessage([](websockets::WebsocketsMessage msg) {
    _handleCommand(msg.data());
  });
  _ws.onEvent([](websockets::WebsocketsEvent event, String data) {
    if (event == websockets::WebsocketsEvent::ConnectionOpened) {
      _connected = true;
      Serial.println("[WS] Connected");
      _needsPush = true;
    } else if (event == websockets::WebsocketsEvent::ConnectionClosed) {
      _connected = false;
      Serial.println("[WS] Disconnected");
    }
  });

  if (serverIp.length() > 0)
    Serial.printf("[WS] Configured: %s:%d\n", serverIp.c_str(), port);
}

inline void tick() {
  if (!_initialized || _serverIp.length() == 0) return;

  if (WiFi.status() != WL_CONNECTED) {
    if (_wifiWas) {
      _wifiWas   = false;
      _connected = false;
      Serial.println("[WS] WiFi lost");
    }
    return;
  }
  if (!_wifiWas) {
    _wifiWas       = true;
    _failCount     = 0;
    _nextReconnect = RECONNECT_MS;
    Serial.println(_wifiEverConnected ? "[WS] WiFi restored" : "[WS] WiFi connected");
    _wifiEverConnected = true;
  }

  // While a connection attempt is running on Core 0, skip poll/send to avoid
  // concurrent access to _ws from two cores.
  if (_connecting) return;

  if (_connected) {
    _ws.poll();

    // State change detection — push on any score or brightness change
    static Score   _lastScore;
    static uint8_t _lastBright = 0;
    xSemaphoreTake(scoreMutex, portMAX_DELAY);
    Score cur = currentScore;
    xSemaphoreGive(scoreMutex);
    uint8_t bright = LED::getBrightness();

    unsigned long now = millis();
    bool anyTimer = ScoreActions::isTimeoutActive() || ScoreActions::isBreakTimerActive();
    unsigned long interval = anyTimer ? 1000UL : HEARTBEAT_MS;
    if (_needsPush                                    ||
        now - _lastPush     >= interval               ||
        cur.scoreA      != _lastScore.scoreA          ||
        cur.scoreB      != _lastScore.scoreB          ||
        cur.setA        != _lastScore.setA            ||
        cur.setB        != _lastScore.setB            ||
        cur.firstServer != _lastScore.firstServer     ||
        cur.winPoints   != _lastScore.winPoints       ||
        cur.hardcap     != _lastScore.hardcap         ||
        bright          != _lastBright) {
      _needsPush  = false;
      _lastPush   = now;
      _lastScore  = cur;
      _lastBright = bright;
      pushState();
    }
    return;
  }

  if (millis() - _lastAttempt >= _nextReconnect) {
    _lastAttempt = millis();
    _connecting  = true;
    if (xTaskCreatePinnedToCore(_connectTaskFn, "ws_conn", 8192, nullptr, 1, nullptr, 0) != pdPASS) {
      _connecting = false;
      Serial.println("[WS] Failed to create connect task");
    }
  }
}

} // namespace WsClient
