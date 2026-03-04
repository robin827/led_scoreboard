/**
 * portal.h - Portail captif moderne
 */

#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "score.h"
#include "led.h"

namespace Portal {

static WebServer *server = nullptr;
static DNSServer dns;

// HTML moderne et minimal
static const char HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Roundnet Scoreboard</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#0a0a0a;color:#fff;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}
.container{width:100%;max-width:400px}
.header{text-align:center;margin-bottom:40px}
.logo{font-size:0.75rem;letter-spacing:4px;text-transform:uppercase;color:#444;margin-bottom:8px;font-weight:600}
.scoreboard{background:#111;border-radius:20px;padding:30px 20px;margin-bottom:20px;box-shadow:0 8px 32px rgba(0,0,0,0.4)}
.scores{display:flex;justify-content:space-around;align-items:center;margin-bottom:30px;position:relative}
.team{text-align:center}
.team-label{font-size:0.7rem;letter-spacing:2px;text-transform:uppercase;color:#555;margin-bottom:12px}
.set-badge{position:absolute;top:50%;transform:translateY(-50%);background:#1a1a1a;border-radius:6px;padding:3px 8px;font-size:0.7rem;font-weight:700;min-width:24px;text-align:center}
.set-badge-a{color:#ff6b35;border:1px solid rgba(255,107,53,0.3);right:calc(50% + 16px)}
.set-badge-b{color:#4a9eff;border:1px solid rgba(74,158,255,0.3);left:calc(50% + 16px)}
.score{font-size:4rem;font-weight:700;line-height:1;font-variant-numeric:tabular-nums}
.score-a{color:#ff6b35}
.score-b{color:#4a9eff}
.divider{width:1px;height:60px;background:#222}
.controls{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:20px}
.btn{border:none;border-radius:12px;padding:16px;font-size:1rem;font-weight:600;cursor:pointer;transition:all .2s;background:#1a1a1a;color:#fff;-webkit-tap-highlight-color:transparent}
.btn:active{transform:scale(0.96)}
.btn-primary-a{background:#ff6b35;color:#fff}
.btn-primary-b{background:#4a9eff;color:#fff}
.btn-secondary-a{background:rgba(255,107,53,0.15);color:#ff6b35;border:1px solid rgba(255,107,53,0.3)}
.btn-secondary-b{background:rgba(74,158,255,0.15);color:#4a9eff;border:1px solid rgba(74,158,255,0.3)}
.actions{display:flex;flex-direction:column;gap:10px;margin-top:20px;padding-top:20px;border-top:1px solid #1a1a1a}
.actions .btn{font-size:0.85rem;background:#1a1a1a;color:#888}
.settings{background:#111;border-radius:20px;padding:20px;margin-top:20px}
.setting-group{margin-bottom:16px}
.setting-group:last-child{margin-bottom:0}
.setting-label{font-size:0.75rem;letter-spacing:1px;text-transform:uppercase;color:#555;margin-bottom:8px;display:block}
.slider-container{position:relative}
.slider{width:100%;height:6px;border-radius:3px;background:#1a1a1a;outline:none;-webkit-appearance:none;appearance:none}
.slider::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:18px;height:18px;border-radius:50%;background:#fff;cursor:pointer;box-shadow:0 2px 8px rgba(0,0,0,0.4)}
.slider::-moz-range-thumb{width:18px;height:18px;border-radius:50%;background:#fff;cursor:pointer;border:none;box-shadow:0 2px 8px rgba(0,0,0,0.4)}
.slider-value{text-align:center;margin-top:8px;font-size:0.85rem;color:#666;font-variant-numeric:tabular-nums}
</style>
</head>
<body>
<div class="container">
  <div class="header">
    <div class="logo">Roundnet Scoreboard</div>
  </div>
  
  <div class="scoreboard">
    <div class="scores">
      <div class="set-badge set-badge-a" id="setA">0</div>
      <div class="set-badge set-badge-b" id="setB">0</div>
      
      <div class="team">
        <div class="team-label">Team A</div>
        <div class="score score-a" id="scoreA">00</div>
      </div>
      <div class="divider"></div>
      <div class="team">
        <div class="team-label">Team B</div>
        <div class="score score-b" id="scoreB">00</div>
      </div>
    </div>
    
    <div class="controls">
      <button class="btn btn-primary-a" onclick="action('/a/inc')">+1</button>
      <button class="btn btn-primary-b" onclick="action('/b/inc')">+1</button>
      <button class="btn btn-secondary-a" onclick="action('/a/dec')">−1</button>
      <button class="btn btn-secondary-b" onclick="action('/b/dec')">−1</button>
    </div>
    
    <div class="actions">
      <button class="btn" onclick="action('/nextset')">Next Set</button>
      <button class="btn" onclick="action('/reset')">Reset Match</button>
    </div>
  </div>
  
  <div class="settings">
    <div class="setting-group">
      <label class="setting-label">Brightness</label>
      <div class="slider-container">
        <input type="range" id="brightness" class="slider" min="10" max="255" value="80" oninput="setBrightness(this.value)">
        <div class="slider-value"><span id="brightVal">80</span> / 255</div>
      </div>
    </div>
  </div>
</div>

<script>
async function action(url) {
  try {
    await fetch(url, {method:'POST'});
    refresh();
  } catch(e) {}
}
async function refresh() {
  try {
    const r = await fetch('/score');
    const d = await r.json();
    document.getElementById('scoreA').textContent = String(d.scoreA).padStart(2,'0');
    document.getElementById('scoreB').textContent = String(d.scoreB).padStart(2,'0');
    document.getElementById('setA').textContent = d.setA;
    document.getElementById('setB').textContent = d.setB;
  } catch(e) {}
}
async function setBrightness(val) {
  document.getElementById('brightVal').textContent = val;
  try {
    await fetch('/brightness', {method:'POST', body: val});
  } catch(e) {}
}
setInterval(refresh, 1000);
refresh();
</script>
</body>
</html>
)rawhtml";

inline void init() {
  Serial.println("[PORTAL] Starting...");
  
  dns.start(53, "*", WiFi.softAPIP());
  server = new WebServer(80);
  
  server->on("/", HTTP_GET, []() {
    server->send_P(200, "text/html", HTML);
  });
  
  server->on("/score", HTTP_GET, []() {
    String json = "{";
    json += "\"scoreA\":" + String(currentScore.scoreA) + ",";
    json += "\"scoreB\":" + String(currentScore.scoreB) + ",";
    json += "\"setA\":" + String(currentScore.setA) + ",";
    json += "\"setB\":" + String(currentScore.setB);
    json += "}";
    server->send(200, "application/json", json);
  });
  
  server->on("/a/inc", HTTP_POST, []() {
    currentScore.incrementA();
    LED::update(currentScore);
    server->send(200, "text/plain", "OK");
  });
  
  server->on("/a/dec", HTTP_POST, []() {
    currentScore.decrementA();
    LED::update(currentScore);
    server->send(200, "text/plain", "OK");
  });
  
  server->on("/b/inc", HTTP_POST, []() {
    currentScore.incrementB();
    LED::update(currentScore);
    server->send(200, "text/plain", "OK");
  });
  
  server->on("/b/dec", HTTP_POST, []() {
    currentScore.decrementB();
    LED::update(currentScore);
    server->send(200, "text/plain", "OK");
  });
  
  server->on("/nextset", HTTP_POST, []() {
    currentScore.nextSet();
    LED::update(currentScore);
    server->send(200, "text/plain", "OK");
  });
  
  server->on("/reset", HTTP_POST, []() {
    currentScore.reset();
    LED::update(currentScore);
    server->send(200, "text/plain", "OK");
  });
  
  server->on("/brightness", HTTP_POST, []() {
    if (server->hasArg("plain")) {
      uint8_t brightness = constrain(server->arg("plain").toInt(), 10, 255);
      FastLED.setBrightness(brightness);
      FastLED.show();
      server->send(200, "text/plain", "OK");
    } else {
      server->send(400, "text/plain", "Bad Request");
    }
  });
  
  server->onNotFound([]() {
    server->sendHeader("Location", "/", true);
    server->send(302, "text/plain", "");
  });
  
  server->begin();
  Serial.printf("[PORTAL] Started on http://%s\n", WiFi.softAPIP().toString().c_str());
}

inline void tick() {
  dns.processNextRequest();
  server->handleClient();
}

} // namespace Portal