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
#include <ArduinoJson.h>

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
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#1c1830;color:#fff;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}
.container{width:100%;max-width:400px}
.header{text-align:center;margin-bottom:32px}
.logo{font-size:0.7rem;letter-spacing:4px;text-transform:uppercase;color:#8070a8;margin-bottom:8px;font-weight:600;display:flex;align-items:center;justify-content:center;gap:8px}
.pulse{width:6px;height:6px;border-radius:50%;background:#3a3460;flex-shrink:0;transition:background .3s}
@keyframes blink{0%,100%{opacity:1}50%{opacity:0.15}}
.pulse.live{animation:blink .5s 1;background:#f5c518}
.scoreboard{background:#251f40;border-radius:20px;padding:28px 20px 20px;margin-bottom:16px;box-shadow:0 8px 40px rgba(0,0,0,0.4)}
.scores{display:flex;justify-content:space-around;align-items:center;margin-bottom:16px;position:relative}
.team{text-align:center}
.team-label{font-size:0.65rem;letter-spacing:2px;text-transform:uppercase;color:#8070a8;margin-bottom:10px}
.set-badge{position:absolute;top:50%;transform:translateY(-50%);background:#3a3460;border-radius:6px;padding:3px 8px;font-size:0.7rem;font-weight:700;min-width:24px;text-align:center}
.set-badge-a{color:#f5c518;border:1px solid rgba(245,197,24,0.35);right:calc(50% + 16px)}
.set-badge-b{color:#e83e8c;border:1px solid rgba(232,62,140,0.35);left:calc(50% + 16px)}
.score{font-size:4rem;font-weight:700;line-height:1;font-variant-numeric:tabular-nums}
.score-a{color:#f5c518}
.score-b{color:#e83e8c}
.divider{width:1px;height:60px;background:#3a3460}
.ratio-bar{display:flex;height:4px;border-radius:2px;overflow:hidden;margin-bottom:20px;gap:2px}
.ratio-a{background:#f5c518;border-radius:2px;transition:flex .4s;flex:1}
.ratio-b{background:#e83e8c;border-radius:2px;transition:flex .4s;flex:1}
.controls{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:16px;transition:opacity .2s}
.btn{border:none;border-radius:12px;padding:16px;font-size:1rem;font-weight:600;cursor:pointer;transition:transform .1s,opacity .1s;background:#3a3460;color:#fff;-webkit-tap-highlight-color:transparent;user-select:none;touch-action:manipulation}
.btn:active{transform:scale(0.96)}
.btn.flash{opacity:0.4;transform:scale(0.93)}
.btn-primary-a{background:#f5c518;color:#1c1830}
.btn-primary-b{background:#e83e8c;color:#fff}
.btn-secondary-a{background:rgba(245,197,24,0.15);color:#f5c518;border:1px solid rgba(245,197,24,0.3)}
.btn-secondary-b{background:rgba(232,62,140,0.15);color:#e83e8c;border:1px solid rgba(232,62,140,0.3)}
.btn-repeat{touch-action:none}
.actions{display:flex;flex-direction:column;gap:10px;padding-top:16px;border-top:1px solid #3a3460;transition:opacity .2s}
.actions .btn{font-size:0.85rem;background:#3a3460;color:#8070a8}
.btn-hold{position:relative;overflow:hidden;touch-action:none}
.btn-hold::after{content:'';position:absolute;left:0;top:0;height:100%;width:0;background:rgba(255,255,255,0.1);border-radius:12px}
.btn-hold.holding::after{width:100%;transition:width .5s linear}
.settings{background:#251f40;border-radius:20px;padding:20px;margin-top:16px}
.setting-group{margin-bottom:20px}
.setting-group:last-child{margin-bottom:0}
.setting-label{font-size:0.68rem;letter-spacing:1px;text-transform:uppercase;color:#8070a8;margin-bottom:10px;display:flex;align-items:center;gap:6px}
.mode-selector{display:flex;gap:8px}
.mode-btn{flex:1;padding:12px;border:none;border-radius:8px;background:#3a3460;color:#8070a8;font-size:0.85rem;font-weight:600;cursor:pointer;transition:all .2s}
.mode-btn.active{background:#f5c518;color:#1c1830}
.mode-hint{font-size:0.75rem;color:#8070a8;margin-top:10px;line-height:1.5;padding:8px 10px;background:#1c1830;border-radius:8px;border-left:2px solid #3a3460}
.input{width:100%;background:#3a3460;border:1px solid #4a4478;border-radius:8px;color:#fff;padding:12px;font-size:0.9rem;outline:none}
.input:focus{border-color:#f5c518}
.slider{width:100%;height:28px;border-radius:14px;background:linear-gradient(to right,#3a3460 0%,#f5c518 100%);outline:none;-webkit-appearance:none;appearance:none;cursor:pointer;margin-bottom:6px}
.slider::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:22px;height:22px;border-radius:50%;background:#f5c518;cursor:pointer;box-shadow:0 2px 10px rgba(0,0,0,0.5)}
.slider::-moz-range-thumb{width:22px;height:22px;border-radius:50%;background:#f5c518;cursor:pointer;border:none;box-shadow:0 2px 10px rgba(0,0,0,0.5)}
.slider-value{text-align:right;font-size:0.8rem;color:#8070a8;font-variant-numeric:tabular-nums}
.network-list{max-height:220px;overflow-y:auto;background:#3a3460;border-radius:8px;margin-top:8px}
.network-item{border-bottom:1px solid #4a4478}
.network-item:last-child{border-bottom:none}
.network-header{padding:12px;cursor:pointer;display:flex;justify-content:space-between;align-items:center}
.network-header:hover,.network-header:active{background:#443e6e}
.network-name{color:#fff;font-size:0.9rem}
.network-signal{color:#8070a8;font-size:0.75rem}
.network-form{display:none;padding:8px 12px 12px;gap:8px}
.network-form.open{display:flex}
.net-pass{flex:1;background:#251f40;border:1px solid #4a4478;border-radius:6px;color:#fff;padding:8px 10px;font-size:0.85rem;outline:none}
.net-pass:focus{border-color:#f5c518}
.net-connect{padding:8px 14px;background:#f5c518;color:#1c1830;border:none;border-radius:6px;font-size:0.85rem;font-weight:600;cursor:pointer;white-space:nowrap}
.btn-scan{width:100%;margin-top:8px;padding:12px;background:#3a3460;color:#8070a8;font-size:0.85rem;border-radius:12px}
.btn-scan:disabled{opacity:0.45;cursor:default}
.btn-disconnect{width:100%;margin-top:8px;padding:10px;background:rgba(232,62,140,0.1);color:#e83e8c;border:1px solid rgba(232,62,140,0.3);border-radius:8px;font-size:0.8rem;font-weight:600;cursor:pointer}
.status-badge{padding:3px 8px;border-radius:6px;font-size:0.7rem;font-weight:600;text-transform:none;letter-spacing:0}
.status-online{background:rgba(245,197,24,0.15);color:#f5c518;border:1px solid rgba(245,197,24,0.35)}
.status-offline{background:rgba(128,112,168,0.15);color:#8070a8;border:1px solid rgba(128,112,168,0.25)}
.status-connecting{background:rgba(232,62,140,0.15);color:#e83e8c;border:1px solid rgba(232,62,140,0.35)}
.wifi-indicator{align-items:center;gap:5px;font-size:0.72rem;color:#f5c518;margin-top:6px;justify-content:center}
</style>
</head>
<body>
<div class="container">
  <div class="header">
    <div class="logo">Roundnet Scoreboard <span id="pulse" class="pulse"></span></div>
    <div id="wifiIndicator" class="wifi-indicator" style="display:none"><svg width="13" height="10" viewBox="0 0 13 10" fill="currentColor" xmlns="http://www.w3.org/2000/svg"><circle cx="6.5" cy="8.5" r="1.2"/><path d="M4 5.8C4.7 5 5.6 4.6 6.5 4.6S8.3 5 9 5.8" stroke="currentColor" stroke-width="1.1" stroke-linecap="round" fill="none"/><path d="M1.5 3C2.9 1.5 4.6.7 6.5.7S10.1 1.5 11.5 3" stroke="currentColor" stroke-width="1.1" stroke-linecap="round" fill="none"/></svg><span id="wifiIndicatorSSID"></span></div>
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

    <div class="ratio-bar"><div class="ratio-a" id="ratioA"></div><div class="ratio-b" id="ratioB"></div></div>

    <div class="controls" id="controls">
      <button class="btn btn-primary-a btn-repeat" onpointerdown="startRepeat('/a/inc',this)" onpointerup="stopRepeat()" onpointerleave="stopRepeat()">+1</button>
      <button class="btn btn-primary-b btn-repeat" onpointerdown="startRepeat('/b/inc',this)" onpointerup="stopRepeat()" onpointerleave="stopRepeat()">+1</button>
      <button class="btn btn-secondary-a" onclick="action('/a/dec',this)">−1</button>
      <button class="btn btn-secondary-b" onclick="action('/b/dec',this)">−1</button>
    </div>

    <div class="actions" id="actions">
      <button class="btn btn-hold" onpointerdown="startHold(this,'/nextset')" onpointerup="cancelHold()" onpointerleave="cancelHold()">Hold: Next Set</button>
      <button class="btn btn-hold" onpointerdown="startHold(this,'/reset')" onpointerup="cancelHold()" onpointerleave="cancelHold()">Hold: Reset</button>
    </div>
  </div>

  <div class="settings">
    <div class="setting-group">
      <label class="setting-label">Brightness</label>
      <input type="range" id="brightness" class="slider" min="1" max="255" value="80" oninput="setBrightness(this.value)">
      <div class="slider-value"><span id="brightVal">31</span>%</div>
    </div>
  </div>

  <div class="settings">
    <div class="setting-group">
      <label class="setting-label">Mode</label>
      <div class="mode-selector">
        <button class="mode-btn" id="modeLocal" onclick="setMode(0)">Local</button>
        <button class="mode-btn" id="modeRead" onclick="setMode(1)">Read</button>
        <button class="mode-btn" id="modeWrite" onclick="setMode(2)">Write</button>
      </div>
      <div class="mode-hint" id="modeHint" style="display:none"></div>
    </div>

    <div class="setting-group" id="channelGroup" style="display:none">
      <label class="setting-label">Channel (Match ID)</label>
      <input type="text" class="input" id="channel" placeholder="ex: match-15" onchange="saveChannel()">
    </div>

    <div class="setting-group" id="wifiGroup" style="display:none">
      <label class="setting-label">WiFi <span id="wifiStatus" class="status-badge status-offline">Offline</span></label>
      <button class="btn btn-scan" id="btnScan" onclick="scanWiFi()">Scan Networks</button>
      <div class="network-list" id="networkList" style="display:none"></div>
      <button class="btn-disconnect" id="btnDisconnect" style="display:none" onclick="disconnectWiFi()">Disconnect from <span id="connectedSSID"></span></button>
    </div>
  </div>
</div>

<script>
let currentMode = 0;
let _online = false;
let _isConnecting = false;
let _repeatTimer = null, _repeatStart = null;
let _holdTimer = null, _holdBtn = null;

async function action(url, btn) {
  if (currentMode === 1) return;
  if (btn) { btn.classList.add('flash'); setTimeout(() => btn.classList.remove('flash'), 150); }
  try { await fetch(url, {method:'POST'}); refresh(); } catch(e) {}
}

function startRepeat(url, btn) {
  if (currentMode === 1) return;
  action(url, btn);
  _repeatStart = setTimeout(() => {
    _repeatTimer = setInterval(() => action(url), 180);
  }, 550);
}

function stopRepeat() {
  clearTimeout(_repeatStart);
  clearInterval(_repeatTimer);
  _repeatStart = _repeatTimer = null;
}

function startHold(btn, url) {
  if (currentMode === 1) return;
  _holdBtn = btn;
  btn.classList.add('holding');
  _holdTimer = setTimeout(async () => {
    btn.classList.remove('holding');
    _holdBtn = null;
    await action(url);
  }, 500);
}

function cancelHold() {
  if (_holdBtn) { _holdBtn.classList.remove('holding'); _holdBtn = null; }
  clearTimeout(_holdTimer);
}

async function refresh() {
  try {
    const r = await fetch('/status');
    const d = await r.json();
    document.getElementById('scoreA').textContent = String(d.scoreA).padStart(2,'0');
    document.getElementById('scoreB').textContent = String(d.scoreB).padStart(2,'0');
    document.getElementById('setA').textContent = d.setA;
    document.getElementById('setB').textContent = d.setB;

    const total = d.scoreA + d.scoreB;
    document.getElementById('ratioA').style.flex = total > 0 ? d.scoreA : 1;
    document.getElementById('ratioB').style.flex = total > 0 ? d.scoreB : 1;

    currentMode = d.mode;
    document.getElementById('modeLocal').classList.toggle('active', d.mode === 0);
    document.getElementById('modeRead').classList.toggle('active', d.mode === 1);
    document.getElementById('modeWrite').classList.toggle('active', d.mode === 2);

    document.getElementById('channelGroup').style.display = d.mode !== 0 ? 'block' : 'none';
    document.getElementById('wifiGroup').style.display = d.mode !== 0 ? 'block' : 'none';

    _online = d.online;

    const hint = document.getElementById('modeHint');
    const wifiLink = ' <a href="#" onclick="document.getElementById(\'wifiGroup\').scrollIntoView({behavior:\'smooth\'});return false" style="color:#f5c518;text-decoration:none;font-weight:600">Connect below \u2193</a>';
    if (d.mode === 1) {
      hint.style.display = 'block';
      hint.innerHTML = 'Read mode: scores are pulled from Firebase. A WiFi connection is required.' + (!d.online ? wifiLink : '');
    } else if (d.mode === 2) {
      hint.style.display = 'block';
      hint.innerHTML = 'Write mode: scores are pushed to Firebase. Still works offline, syncs when connected.' + (!d.online ? wifiLink : '');
    } else { hint.style.display = 'none'; }

    const readMode = d.mode === 1;
    const cs = document.getElementById('controls').style;
    const as = document.getElementById('actions').style;
    cs.opacity = as.opacity = readMode ? '0.3' : '1';
    cs.pointerEvents = as.pointerEvents = readMode ? 'none' : '';

    if (d.channel) document.getElementById('channel').value = d.channel;

    if (d.brightness !== undefined && !_brightnessDirty) {
      document.getElementById('brightness').value = d.brightness;
      document.getElementById('brightVal').textContent = Math.round(d.brightness / 255 * 100);
    }

    const indicator = document.getElementById('wifiIndicator');
    const status = document.getElementById('wifiStatus');
    const btnDisc = document.getElementById('btnDisconnect');
    if (d.online && d.ssid) {
      _isConnecting = false;
      indicator.style.display = d.mode === 0 ? 'flex' : 'none';
      document.getElementById('wifiIndicatorSSID').textContent = d.ssid;
      status.className = 'status-badge status-online';
      status.textContent = d.ssid + (d.rssi ? ' \u00b7 ' + d.rssi + ' dBm' : '');
      document.getElementById('connectedSSID').textContent = d.ssid;
      btnDisc.style.display = 'block';
    } else if (_isConnecting) {
      indicator.style.display = 'none';
      status.className = 'status-badge status-connecting';
      status.textContent = 'Connecting\u2026';
      btnDisc.style.display = 'none';
    } else {
      indicator.style.display = 'none';
      status.className = 'status-badge status-offline';
      status.textContent = 'Offline';
      btnDisc.style.display = 'none';
    }

    const p = document.getElementById('pulse');
    p.classList.remove('live');
    void p.offsetWidth;
    p.classList.add('live');
  } catch(e) {}
}

async function setMode(mode) {
  try {
    await fetch('/mode', {method:'POST', body: String(mode)});
    await refresh();
    if (mode !== 0 && !_online) {
      document.getElementById('wifiGroup').scrollIntoView({behavior:'smooth'});
      scanWiFi();
    }
  } catch(e) {}
}

async function saveChannel() {
  try { await fetch('/channel', {method:'POST', body: document.getElementById('channel').value}); } catch(e) {}
}

async function scanWiFi() {
  const btn = document.getElementById('btnScan');
  btn.disabled = true;
  btn.textContent = 'Scanning\u2026';
  try {
    const r = await fetch('/wifi/scan');
    const nets = await r.json();
    const list = document.getElementById('networkList');
    list.innerHTML = '';
    nets.forEach(net => {
      const item = document.createElement('div');
      item.className = 'network-item';
      item.dataset.ssid = net.ssid;
      item.dataset.secure = net.secure ? '1' : '0';
      const hdr = document.createElement('div');
      hdr.className = 'network-header';
      const name = document.createElement('span');
      name.className = 'network-name';
      name.textContent = net.ssid;
      const sig = document.createElement('span');
      sig.className = 'network-signal';
      sig.textContent = net.rssi + ' dBm';
      hdr.appendChild(name);
      hdr.appendChild(sig);
      hdr.addEventListener('click', () => toggleForm(item));
      const form = document.createElement('div');
      form.className = 'network-form';
      if (net.secure) {
        const pw = document.createElement('input');
        pw.type = 'password';
        pw.placeholder = 'Password';
        pw.className = 'net-pass';
        form.appendChild(pw);
      }
      const cb = document.createElement('button');
      cb.className = 'net-connect';
      cb.textContent = 'Connect';
      cb.addEventListener('click', e => { e.stopPropagation(); submitConnect(item); });
      form.appendChild(cb);
      item.appendChild(hdr);
      item.appendChild(form);
      list.appendChild(item);
    });
    list.style.display = nets.length ? 'block' : 'none';
  } catch(e) {}
  btn.disabled = false;
  btn.textContent = 'Scan Networks';
}

function toggleForm(item) {
  const form = item.querySelector('.network-form');
  const wasOpen = form.classList.contains('open');
  document.querySelectorAll('.network-form.open').forEach(f => f.classList.remove('open'));
  if (!wasOpen) {
    form.classList.add('open');
    const pw = form.querySelector('.net-pass');
    if (pw) pw.focus();
  }
}

function submitConnect(item) {
  const ssid = item.dataset.ssid;
  const pass = (item.querySelector('.net-pass') || {value:''}).value;
  fetch('/wifi/connect', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({ssid, pass})});
  _isConnecting = true;
  document.getElementById('networkList').style.display = 'none';
  const s = document.getElementById('wifiStatus');
  s.className = 'status-badge status-connecting';
  s.textContent = 'Connecting\u2026';
  setTimeout(refresh, 1000);
}

function disconnectWiFi() {
  if (!confirm('Disconnect from WiFi?')) return;
  fetch('/wifi/disconnect', {method:'POST'}).then(() => { _isConnecting = false; setTimeout(refresh, 500); });
}

let _brightnessTimer = null;
let _brightnessDirty = false;
function setBrightness(val) {
  document.getElementById('brightVal').textContent = Math.round(val / 255 * 100);
  _brightnessDirty = true;
  clearTimeout(_brightnessTimer);
  _brightnessTimer = setTimeout(async () => {
    try { await fetch('/brightness', {method:'POST', body: val}); } catch(e) {}
    _brightnessDirty = false;
  }, 300);
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
    xSemaphoreTake(scoreMutex, portMAX_DELAY);
    uint8_t sA = currentScore.scoreA, sB = currentScore.scoreB;
    uint8_t sSetA = currentScore.setA, sSetB = currentScore.setB;
    xSemaphoreGive(scoreMutex);
    String json = "{";
    json += "\"scoreA\":" + String(sA) + ",";
    json += "\"scoreB\":" + String(sB) + ",";
    json += "\"setA\":" + String(sSetA) + ",";
    json += "\"setB\":" + String(sSetB) + ",";
    json += "\"mode\":" + String((int)Mode::get()) + ",";
    json += "\"channel\":\"" + Firebase::getChannel() + "\",";
    json += "\"brightness\":" + String(LED::getBrightness()) + ",";
    json += "\"online\":" + String(WiFiMgr::isOnline() ? "true" : "false") + ",";
    json += "\"ssid\":\"" + WiFiMgr::getSSID() + "\",";
    json += "\"rssi\":" + String(WiFiMgr::getRSSI());
    json += "}";
    server->send(200, "application/json", json);
  });
  
  // Actions score (modes LOCAL et WRITE)
  server->on("/a/inc", HTTP_POST, []() {
    if (!Mode::isRead()) {
      xSemaphoreTake(scoreMutex, portMAX_DELAY);
      currentScore.incrementA();
      LED::update(currentScore);
      xSemaphoreGive(scoreMutex);
    }
    server->send(200, "text/plain", "OK");
  });

  server->on("/a/dec", HTTP_POST, []() {
    if (!Mode::isRead()) {
      xSemaphoreTake(scoreMutex, portMAX_DELAY);
      currentScore.decrementA();
      LED::update(currentScore);
      xSemaphoreGive(scoreMutex);
    }
    server->send(200, "text/plain", "OK");
  });

  server->on("/b/inc", HTTP_POST, []() {
    if (!Mode::isRead()) {
      xSemaphoreTake(scoreMutex, portMAX_DELAY);
      currentScore.incrementB();
      LED::update(currentScore);
      xSemaphoreGive(scoreMutex);
    }
    server->send(200, "text/plain", "OK");
  });

  server->on("/b/dec", HTTP_POST, []() {
    if (!Mode::isRead()) {
      xSemaphoreTake(scoreMutex, portMAX_DELAY);
      currentScore.decrementB();
      LED::update(currentScore);
      xSemaphoreGive(scoreMutex);
    }
    server->send(200, "text/plain", "OK");
  });

  server->on("/nextset", HTTP_POST, []() {
    if (!Mode::isRead()) {
      xSemaphoreTake(scoreMutex, portMAX_DELAY);
      currentScore.nextSet();
      LED::update(currentScore);
      xSemaphoreGive(scoreMutex);
    }
    server->send(200, "text/plain", "OK");
  });

  server->on("/reset", HTTP_POST, []() {
    if (!Mode::isRead()) {
      xSemaphoreTake(scoreMutex, portMAX_DELAY);
      currentScore.reset();
      LED::update(currentScore);
      xSemaphoreGive(scoreMutex);
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
      JsonDocument doc;
      if (!deserializeJson(doc, server->arg("plain"))) {
        const char* ssid = doc["ssid"];
        const char* pass = doc["pass"] | "";
        if (ssid && strlen(ssid) > 0) {
          WiFiMgr::saveCredentials(String(ssid), String(pass));
          WiFiMgr::resetRetryCount();
          server->send(200, "text/plain", "OK");
          return;
        }
      }
    }
    server->send(400, "text/plain", "Bad Request");
  });
  
  // WiFi disconnect
  server->on("/wifi/disconnect", HTTP_POST, []() {
    WiFiMgr::clearCredentials();
    WiFi.disconnect();
    server->send(200, "text/plain", "OK");
  });
  
  // WiFi reset (efface credentials)
  server->on("/wifi/reset", HTTP_POST, []() {
    WiFiMgr::clearCredentials();
    WiFiMgr::resetRetryCount();
    WiFi.disconnect();
    server->send(200, "text/plain", "Credentials cleared");
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
  
  // Redirection captive portal
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