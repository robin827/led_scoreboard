/**
 * portal.h - Portail captif avec mode LOCAL/READ + config WiFi
 */

#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "score.h"
#include "led.h"
#include "mode.h"
#include "wifi_mgr.h"
#include "firebase.h"

namespace Portal {

static WebServer *server = nullptr;
static DNSServer dns;

// HTML moderne avec mode selector + channel + WiFi scan
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
.setting-group{margin-bottom:20px}
.setting-group:last-child{margin-bottom:0}
.setting-label{font-size:0.75rem;letter-spacing:1px;text-transform:uppercase;color:#555;margin-bottom:8px;display:block}
.mode-selector{display:flex;gap:8px}
.mode-btn{flex:1;padding:12px;border:none;border-radius:8px;background:#1a1a1a;color:#666;font-size:0.85rem;font-weight:600;cursor:pointer;transition:all .2s}
.mode-btn.active{background:#4a9eff;color:#fff}
.input{width:100%;background:#1a1a1a;border:1px solid #222;border-radius:8px;color:#fff;padding:12px;font-size:0.9rem;outline:none}
.input:focus{border-color:#4a9eff}
.slider{width:100%;height:6px;border-radius:3px;background:#1a1a1a;outline:none;-webkit-appearance:none;appearance:none}
.slider::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:18px;height:18px;border-radius:50%;background:#fff;cursor:pointer;box-shadow:0 2px 8px rgba(0,0,0,0.4)}
.slider::-moz-range-thumb{width:18px;height:18px;border-radius:50%;background:#fff;cursor:pointer;border:none;box-shadow:0 2px 8px rgba(0,0,0,0.4)}
.slider-value{text-align:center;margin-top:8px;font-size:0.85rem;color:#666;font-variant-numeric:tabular-nums}
.network-list{max-height:200px;overflow-y:auto;background:#1a1a1a;border-radius:8px;margin-top:8px}
.network-item{padding:12px;border-bottom:1px solid #222;cursor:pointer;display:flex;justify-content:space-between;align-items:center}
.network-item:last-child{border-bottom:none}
.network-item:hover{background:#222}
.network-name{color:#fff;font-size:0.9rem}
.network-signal{color:#666;font-size:0.75rem}
.btn-scan{width:100%;margin-top:8px;padding:12px;background:#1a1a1a;color:#888;font-size:0.85rem}
.status-badge{display:inline-block;padding:4px 8px;border-radius:6px;font-size:0.7rem;font-weight:600;margin-left:8px}
.status-online{background:rgba(74,158,255,0.15);color:#4a9eff;border:1px solid rgba(74,158,255,0.3)}
.status-offline{background:rgba(136,136,136,0.15);color:#888;border:1px solid rgba(136,136,136,0.3)}
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
    
    <div class="controls" id="controls">
      <button class="btn btn-primary-a" onclick="action('/a/inc')">+1</button>
      <button class="btn btn-primary-b" onclick="action('/b/inc')">+1</button>
      <button class="btn btn-secondary-a" onclick="action('/a/dec')">−1</button>
      <button class="btn btn-secondary-b" onclick="action('/b/dec')">−1</button>
    </div>
    
    <div class="actions" id="actions">
      <button class="btn" onclick="action('/nextset')">Next Set</button>
      <button class="btn" onclick="action('/reset')">Reset Match</button>
    </div>
  </div>
  
  <div class="settings">
    <div class="setting-group">
      <label class="setting-label">Mode</label>
      <div class="mode-selector">
        <button class="mode-btn" id="modeLocal" onclick="setMode(0)">Local</button>
        <button class="mode-btn" id="modeRead" onclick="setMode(1)">Read</button>
      </div>
    </div>
    
    <div class="setting-group" id="channelGroup" style="display:none">
      <label class="setting-label">Channel (Match ID)</label>
      <input type="text" class="input" id="channel" placeholder="ex: match-15" onchange="saveChannel()">
    </div>
    
    <div class="setting-group">
      <label class="setting-label">WiFi <span id="wifiStatus" class="status-badge status-offline">Offline</span></label>
      <button class="btn btn-scan" onclick="scanWiFi()">Scan Networks</button>
      <div class="network-list" id="networkList" style="display:none"></div>
    </div>
    
    <div class="setting-group">
      <label class="setting-label">Brightness</label>
      <input type="range" id="brightness" class="slider" min="1" max="255" value="80" oninput="setBrightness(this.value)">
      <div class="slider-value"><span id="brightVal">80</span> / 255</div>
    </div>
  </div>
</div>

<script>
let currentMode = 0;

async function action(url) {
  if (currentMode === 1) return; // Pas d'actions en mode READ
  try {
    await fetch(url, {method:'POST'});
    refresh();
  } catch(e) {}
}

async function refresh() {
  try {
    const r = await fetch('/status');
    const d = await r.json();
    document.getElementById('scoreA').textContent = String(d.scoreA).padStart(2,'0');
    document.getElementById('scoreB').textContent = String(d.scoreB).padStart(2,'0');
    document.getElementById('setA').textContent = d.setA;
    document.getElementById('setB').textContent = d.setB;
    
    // Mode
    currentMode = d.mode;
    document.getElementById('modeLocal').classList.toggle('active', d.mode === 0);
    document.getElementById('modeRead').classList.toggle('active', d.mode === 1);
    document.getElementById('channelGroup').style.display = d.mode === 1 ? 'block' : 'none';
    document.getElementById('controls').style.opacity = d.mode === 1 ? '0.3' : '1';
    document.getElementById('actions').style.opacity = d.mode === 1 ? '0.3' : '1';
    
    // Channel
    if (d.channel) document.getElementById('channel').value = d.channel;
    
    // Brightness
    if (d.brightness !== undefined) {
      document.getElementById('brightness').value = d.brightness;
      document.getElementById('brightVal').textContent = d.brightness;
    }
    
    // WiFi status
    const status = document.getElementById('wifiStatus');
    if (d.online) {
      status.className = 'status-badge status-online';
      status.textContent = 'Online';
    } else {
      status.className = 'status-badge status-offline';
      status.textContent = 'Offline';
    }
  } catch(e) {}
}

async function setMode(mode) {
  try {
    await fetch('/mode', {method:'POST', body: String(mode)});
    refresh();
  } catch(e) {}
}

async function saveChannel() {
  const channel = document.getElementById('channel').value;
  try {
    await fetch('/channel', {method:'POST', body: channel});
  } catch(e) {}
}

async function scanWiFi() {
  try {
    const r = await fetch('/wifi/scan');
    const networks = await r.json();
    const list = document.getElementById('networkList');
    list.innerHTML = '';
    networks.forEach(net => {
      const item = document.createElement('div');
      item.className = 'network-item';
      item.onclick = () => connectWiFi(net.ssid, net.secure);
      item.innerHTML = `<span class="network-name">${net.ssid}</span><span class="network-signal">${net.rssi} dBm</span>`;
      list.appendChild(item);
    });
    list.style.display = 'block';
  } catch(e) {}
}

function connectWiFi(ssid, secure) {
  const pass = secure ? prompt(`Password for ${ssid}:`) : '';
  if (pass === null && secure) return;
  fetch('/wifi/connect', {
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body: JSON.stringify({ssid, pass})
  });
}

async function setBrightness(val) {
  document.getElementById('brightVal').textContent = val;
  try {
    await fetch('/brightness', {method:'POST', body: val});
  } catch(e) {}
}

setInterval(refresh, 2000);
refresh();
</script>
</body>
</html>
)rawhtml";

inline void init() {
  Serial.println("[PORTAL] Starting...");
  
  dns.start(53, "*", WiFi.softAPIP());
  server = new WebServer(80);
  
  // Page principale
  server->on("/", HTTP_GET, []() {
    server->send_P(200, "text/html", HTML);
  });
  
  // Status JSON (score + mode + WiFi + brightness)
  server->on("/status", HTTP_GET, []() {
    String json = "{";
    json += "\"scoreA\":" + String(currentScore.scoreA) + ",";
    json += "\"scoreB\":" + String(currentScore.scoreB) + ",";
    json += "\"setA\":" + String(currentScore.setA) + ",";
    json += "\"setB\":" + String(currentScore.setB) + ",";
    json += "\"mode\":" + String((int)Mode::get()) + ",";
    json += "\"channel\":\"" + Firebase::getChannel() + "\",";
    json += "\"brightness\":" + String(LED::getBrightness()) + ",";
    json += "\"online\":" + String(WiFiMgr::isOnline() ? "true" : "false");
    json += "}";
    server->send(200, "application/json", json);
  });
  
  // Actions score (seulement en mode LOCAL)
  server->on("/a/inc", HTTP_POST, []() {
    if (Mode::isLocal()) {
      currentScore.incrementA();
      LED::update(currentScore);
    }
    server->send(200, "text/plain", "OK");
  });
  
  server->on("/a/dec", HTTP_POST, []() {
    if (Mode::isLocal()) {
      currentScore.decrementA();
      LED::update(currentScore);
    }
    server->send(200, "text/plain", "OK");
  });
  
  server->on("/b/inc", HTTP_POST, []() {
    if (Mode::isLocal()) {
      currentScore.incrementB();
      LED::update(currentScore);
    }
    server->send(200, "text/plain", "OK");
  });
  
  server->on("/b/dec", HTTP_POST, []() {
    if (Mode::isLocal()) {
      currentScore.decrementB();
      LED::update(currentScore);
    }
    server->send(200, "text/plain", "OK");
  });
  
  server->on("/nextset", HTTP_POST, []() {
    if (Mode::isLocal()) {
      currentScore.nextSet();
      LED::update(currentScore);
    }
    server->send(200, "text/plain", "OK");
  });
  
  server->on("/reset", HTTP_POST, []() {
    if (Mode::isLocal()) {
      currentScore.reset();
      LED::update(currentScore);
    }
    server->send(200, "text/plain", "OK");
  });
  
  // Mode
  server->on("/mode", HTTP_POST, []() {
    if (server->hasArg("plain")) {
      int mode = server->arg("plain").toInt();
      Mode::set((AppMode)mode);
      server->send(200, "text/plain", "OK");
    } else {
      server->send(400, "text/plain", "Bad Request");
    }
  });
  
  // Channel
  server->on("/channel", HTTP_POST, []() {
    if (server->hasArg("plain")) {
      Firebase::setChannel(server->arg("plain"));
      server->send(200, "text/plain", "OK");
    } else {
      server->send(400, "text/plain", "Bad Request");
    }
  });
  
  // WiFi scan
  server->on("/wifi/scan", HTTP_GET, []() {
    String json = WiFiMgr::scanNetworks();
    server->send(200, "application/json", json);
  });
  
  // WiFi connect
  server->on("/wifi/connect", HTTP_POST, []() {
    if (server->hasArg("plain")) {
      String body = server->arg("plain");
      int idxSsid = body.indexOf("\"ssid\":\"");
      int idxPass = body.indexOf("\"pass\":\"");
      if (idxSsid >= 0) {
        int startSsid = idxSsid + 8;
        int endSsid = body.indexOf("\"", startSsid);
        String ssid = body.substring(startSsid, endSsid);
        
        String pass = "";
        if (idxPass >= 0) {
          int startPass = idxPass + 8;
          int endPass = body.indexOf("\"", startPass);
          pass = body.substring(startPass, endPass);
        }
        
        WiFiMgr::saveCredentials(ssid, pass);
        WiFiMgr::resetRetryCount();
        server->send(200, "text/plain", "OK");
        return;
      }
    }
    server->send(400, "text/plain", "Bad Request");
  });
  
  // Brightness
  server->on("/brightness", HTTP_POST, []() {
    if (server->hasArg("plain")) {
      uint8_t brightness = constrain(server->arg("plain").toInt(), 1, 255);
      LED::setBrightness(brightness);  // Sauvegarde automatiquement en NVS
      server->send(200, "text/plain", "OK");
    } else {
      server->send(400, "text/plain", "Bad Request");
    }
  });
  
  // Redirection
  server->onNotFound([]() {
    server->sendHeader("Location", "/", true);
    server->send(302, "text/plain", "");
  });
  
  // Routes spéciales pour détection captive portal Android/iOS
  server->on("/generate_204", HTTP_GET, []() {
    server->sendHeader("Location", "/", true);
    server->send(302, "text/plain", "");
  });
  
  server->on("/hotspot-detect.html", HTTP_GET, []() {
    server->sendHeader("Location", "/", true);
    server->send(302, "text/plain", "");
  });
  
  server->on("/canonical.html", HTTP_GET, []() {
    server->sendHeader("Location", "/", true);
    server->send(302, "text/plain", "");
  });
  
  server->on("/success.txt", HTTP_GET, []() {
    server->send(200, "text/plain", "success");
  });
  
  server->begin();
  Serial.printf("[PORTAL] Started on http://%s\n", WiFi.softAPIP().toString().c_str());
}

inline void tick() {
  dns.processNextRequest();
  server->handleClient();
}

} // namespace Portal