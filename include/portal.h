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
#include "wifi_mgr.h"
#include "score_actions.h"
#include <ArduinoJson.h>
#include "ws_client.h"
#include "mode.h"
#include "firebase.h"
#include <Update.h>

// Defined in main.cpp — forward declarations so the /mode route can manage the Firebase task
extern TaskHandle_t firebaseTaskHandle;
extern void firebaseTask(void*);

namespace Portal {

static WebServer *server           = nullptr;
static DNSServer  dns;
static bool       _serviceOnline   = false;   // confirmed state driving service lifecycle

// Overlay video cache — stored in PSRAM (or heap fallback) so /overlay-download serves it as HTTP
static uint8_t*  _ovCache     = nullptr;
static size_t    _ovCapacity  = 0;
static size_t    _ovCacheSize = 0;
static char      _ovMime[24]  = "video/webm";
static constexpr size_t _OV_MAX = 5UL * 1024UL * 1024UL; // 5 MB ideal cap

static void _ovAlloc() {
  // Always reallocate so each export gets the freshest available block
  if (_ovCache) { free(_ovCache); _ovCache = nullptr; _ovCapacity = 0; }
  _ovCache = (uint8_t*)heap_caps_malloc(_OV_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (_ovCache) { _ovCapacity = _OV_MAX; return; }
  size_t avail = ESP.getMaxAllocHeap();
  _ovCapacity = avail > 16*1024 ? avail - 8*1024 : 0;
  if (_ovCapacity) _ovCache = (uint8_t*)malloc(_ovCapacity);
  if (!_ovCache) _ovCapacity = 0;
}

static void _handleOverlayUpload() {
  HTTPUpload& up = server->upload();
  if (up.status == UPLOAD_FILE_START) {
    _ovAlloc();
    _ovCacheSize = 0;
    if (up.type.length()) strncpy(_ovMime, up.type.c_str(), sizeof(_ovMime) - 1);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (_ovCache && _ovCacheSize + up.currentSize <= _ovCapacity) {
      memcpy(_ovCache + _ovCacheSize, up.buf, up.currentSize);
      _ovCacheSize += up.currentSize;
    }
  }
}
static uint32_t   _offlineGraceStart = 0;
static constexpr uint32_t OFFLINE_GRACE_MS = 5000;

// HTML moderne avec mode selector + channel + WiFi scan
static const char HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Roundnet Scoreboard</title>
<style>
:root{
--bg:#1c1830;--card:#251f40;--elem:#3a3460;--border:#4a4478;--hover:#443e6e;
--accent:#8070a8;--a:#f5c518;--b:#e83e8c;
--a-rgb:245,197,24;--b-rgb:232,62,140;--accent-rgb:128,112,168;
}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:var(--bg);color:#fff;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}
body.top-align{align-items:flex-start}
.container{width:100%;max-width:400px}
.header{text-align:center;margin-bottom:32px}
.logo{font-size:0.7rem;letter-spacing:4px;text-transform:uppercase;color:var(--accent);margin-bottom:8px;font-weight:600;display:flex;align-items:center;justify-content:center;gap:8px}
.pulse{width:6px;height:6px;border-radius:50%;background:var(--elem);flex-shrink:0;transition:background .3s}
@keyframes blink{0%,100%{opacity:1}50%{opacity:0.15}}
.pulse.live{animation:blink .5s 1;background:var(--a)}
.scoreboard{background:var(--card);border-radius:20px;padding:28px 20px 20px;margin-bottom:16px;box-shadow:0 8px 40px rgba(0,0,0,0.4);position:relative}
.scores{display:flex;justify-content:space-around;align-items:center;margin-bottom:16px;position:relative}
.team{text-align:center}
.team-label{font-size:0.65rem;letter-spacing:2px;text-transform:uppercase;color:var(--accent);margin-bottom:10px;cursor:pointer;-webkit-tap-highlight-color:transparent;user-select:none;position:relative}
.team-label:active{opacity:0.6}
.set-badge{position:absolute;top:50%;transform:translateY(-50%);background:var(--elem);border-radius:6px;padding:3px 8px;font-size:0.7rem;font-weight:700;min-width:24px;text-align:center}
.set-badge-a{color:var(--a);border:1px solid rgba(var(--a-rgb),0.35);right:calc(50% + 16px)}
.set-badge-b{color:var(--b);border:1px solid rgba(var(--b-rgb),0.35);left:calc(50% + 16px)}
.score{font-size:4rem;font-weight:700;line-height:1;font-variant-numeric:tabular-nums}
.score-a{color:var(--a)}
.score-b{color:var(--b)}
.divider{width:1px;height:60px;background:var(--elem)}
.ratio-bar{display:flex;height:4px;border-radius:2px;overflow:hidden;margin-bottom:20px;gap:2px}
.ratio-a{background:var(--a);border-radius:2px;transition:flex .4s;flex:1}
.ratio-b{background:var(--b);border-radius:2px;transition:flex .4s;flex:1}
.controls{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:16px}
.btn{border:none;border-radius:12px;padding:16px;font-size:1rem;font-weight:600;cursor:pointer;transition:transform .1s,opacity .1s;background:var(--elem);color:#fff;-webkit-tap-highlight-color:transparent;user-select:none;touch-action:manipulation}
.btn:active{transform:scale(0.96)}
.btn.flash{opacity:0.4;transform:scale(0.93)}
.btn-primary-a{background:var(--a);color:var(--bg)}
.btn-primary-b{background:var(--b);color:#fff}
.btn-secondary-a{background:rgba(var(--a-rgb),0.15);color:var(--a);border:1px solid rgba(var(--a-rgb),0.3)}
.btn-secondary-b{background:rgba(var(--b-rgb),0.15);color:var(--b);border:1px solid rgba(var(--b-rgb),0.3)}
.btn-repeat{touch-action:none}
.actions{display:flex;flex-direction:column;gap:10px;padding-top:16px;border-top:1px solid var(--elem)}
.actions .btn{font-size:0.85rem;background:var(--elem);color:var(--accent)}
.btn-hold{position:relative;overflow:hidden;touch-action:none}
.btn-hold::after{content:'';position:absolute;left:0;top:0;height:100%;width:0;background:rgba(255,255,255,0.1);border-radius:12px}
.btn-hold.holding::after{width:100%;transition:width .5s linear}
.settings{background:var(--card);border-radius:20px;padding:20px;margin-top:16px}
.setting-group{margin-bottom:20px}
.setting-group:last-child{margin-bottom:0}
.setting-label{font-size:0.68rem;letter-spacing:1px;text-transform:uppercase;color:var(--accent);margin-bottom:10px;display:flex;align-items:center;gap:6px}
.mode-selector{display:flex;gap:8px}
.mode-btn{flex:1;padding:10px 8px;border:none;border-radius:8px;background:var(--elem);color:var(--accent);font-size:0.85rem;font-weight:600;cursor:pointer;transition:all .2s;display:flex;flex-direction:column;align-items:center;gap:3px}
.mode-btn.active{background:var(--a);color:var(--bg)}
.mode-sub{font-size:0.6rem;font-weight:400;opacity:0.65;letter-spacing:0;text-transform:none;line-height:1.2;text-align:center}
.mode-btn.active .mode-sub{opacity:0.7}
.mode-hint{font-size:0.75rem;color:var(--accent);margin-top:10px;line-height:1.5;padding:8px 10px;background:var(--bg);border-radius:8px;border-left:2px solid var(--elem)}
.input{width:100%;background:var(--elem);border:1px solid var(--border);border-radius:8px;color:#fff;padding:12px;font-size:0.9rem;outline:none}
.input:focus{border-color:var(--a)}
select.input{appearance:none;cursor:pointer;background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='8' viewBox='0 0 12 8'%3E%3Cpath d='M1 1l5 5 5-5' stroke='%238070a8' stroke-width='1.5' fill='none' stroke-linecap='round'/%3E%3C/svg%3E");background-repeat:no-repeat;background-position:right 12px center;padding-right:32px}
select.input option{background:var(--elem)}
.slider{width:100%;height:28px;border-radius:14px;background:linear-gradient(to right,var(--elem) 0%,var(--a) 100%);outline:none;-webkit-appearance:none;appearance:none;cursor:pointer;margin-bottom:6px}
.slider::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:22px;height:22px;border-radius:50%;background:var(--a);cursor:pointer;box-shadow:0 2px 10px rgba(0,0,0,0.5)}
.slider::-moz-range-thumb{width:22px;height:22px;border-radius:50%;background:var(--a);cursor:pointer;border:none;box-shadow:0 2px 10px rgba(0,0,0,0.5)}
.slider-value{text-align:right;font-size:0.8rem;color:var(--accent);font-variant-numeric:tabular-nums}
.network-list{max-height:220px;overflow-y:auto;background:var(--elem);border-radius:8px;margin-top:8px}
.network-item{border-bottom:1px solid var(--border)}
.network-item:last-child{border-bottom:none}
.network-header{padding:12px;cursor:pointer;display:flex;justify-content:space-between;align-items:center}
.network-header:hover,.network-header:active{background:var(--hover)}
.network-name{color:#fff;font-size:0.9rem}
.network-signal{color:var(--accent);font-size:0.75rem}
.network-form{display:none;padding:8px 12px 12px;gap:8px}
.network-form.open{display:flex}
.net-pass{flex:1;background:var(--card);border:1px solid var(--border);border-radius:6px;color:#fff;padding:8px 10px;font-size:0.85rem;outline:none}
.net-pass:focus{border-color:var(--a)}
.net-connect{padding:8px 14px;background:var(--a);color:var(--bg);border:none;border-radius:6px;font-size:0.85rem;font-weight:600;cursor:pointer;white-space:nowrap}
.btn-scan{width:100%;margin-top:8px;padding:12px;background:var(--elem);color:var(--accent);font-size:0.85rem;border-radius:12px;display:flex;align-items:center;justify-content:center;gap:8px}
.btn-scan:disabled{opacity:0.7;cursor:default}
@keyframes spin{to{transform:rotate(360deg)}}
.spinner{width:14px;height:14px;border:2px solid rgba(var(--accent-rgb),0.3);border-top-color:var(--accent);border-radius:50%;animation:spin .7s linear infinite;flex-shrink:0}
.btn-disconnect{width:100%;margin-top:8px;padding:10px;background:rgba(var(--b-rgb),0.1);color:var(--b);border:1px solid rgba(var(--b-rgb),0.3);border-radius:8px;font-size:0.8rem;font-weight:600;cursor:pointer}
.status-badge{padding:3px 8px;border-radius:6px;font-size:0.7rem;font-weight:600;text-transform:none;letter-spacing:0}
.status-online{background:rgba(var(--a-rgb),0.15);color:var(--a);border:1px solid rgba(var(--a-rgb),0.35)}
.status-offline{background:rgba(var(--accent-rgb),0.15);color:var(--accent);border:1px solid rgba(var(--accent-rgb),0.25)}
.status-connecting{background:rgba(var(--b-rgb),0.15);color:var(--b);border:1px solid rgba(var(--b-rgb),0.35)}
.score-wrap{position:relative;display:flex;align-items:center;justify-content:center}
.serve-col{position:absolute;display:flex;flex-direction:column;justify-content:center;gap:5px;width:7px;height:52px}
.serve-col-a{left:-14px}.serve-col-b{right:-14px}
.serve-dot{width:7px;height:7px;border-radius:50%;transition:opacity .3s}
.serve-dot-a{background:var(--a)}.serve-dot-b{background:var(--b)}
.serve-dot-dim{opacity:0.35}
.srv-hint{position:absolute;top:-6px;left:calc(50% + 30px);font-size:0.5rem;padding:1px 3px;border-radius:3px;letter-spacing:0;text-transform:none;font-weight:700;background:var(--elem);color:var(--accent);opacity:0.35;transition:all .2s;white-space:nowrap}
.lbl-a.first-srv .srv-hint{opacity:1;background:rgba(var(--a-rgb),0.18);color:var(--a)}
.lbl-b.first-srv .srv-hint{opacity:1;background:rgba(var(--b-rgb),0.18);color:var(--b)}
.wifi-indicator{align-items:center;gap:5px;font-size:0.72rem;color:var(--a);margin-top:6px;justify-content:center}
@keyframes toastIn{from{opacity:0;transform:translate(-50%,-50%) scale(0.9)}to{opacity:1;transform:translate(-50%,-50%) scale(1)}}
.toast{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);background:var(--elem);color:var(--b);border:1px solid rgba(var(--b-rgb),0.35);border-radius:10px;padding:10px 18px;font-size:0.82rem;font-weight:600;white-space:nowrap;pointer-events:none;animation:toastIn .2s ease;z-index:999}
.set-history{display:flex;justify-content:center;gap:8px;margin-bottom:16px}
.set-chip{display:flex;align-items:center;gap:5px;background:var(--bg);border-radius:8px;padding:5px 11px;font-size:0.78rem;font-variant-numeric:tabular-nums}
.set-chip-lbl{font-size:0.5rem;letter-spacing:1px;text-transform:uppercase;color:var(--border);margin-right:2px}
.set-chip-score{font-weight:700;color:var(--border)}
.set-chip-score.win-a{color:var(--a)}
.set-chip-score.win-b{color:var(--b)}
.set-chip-sep{color:var(--elem);margin:0 1px}
.modal-overlay{position:fixed;inset:0;background:rgba(0,0,0,0.72);display:flex;align-items:center;justify-content:center;z-index:1000;opacity:0;pointer-events:none;transition:opacity .2s}
.modal-overlay.open{opacity:1;pointer-events:all}
.modal{background:var(--card);border-radius:20px;padding:28px 24px;width:calc(100% - 48px);max-width:320px;transform:scale(0.9);transition:transform .2s;box-shadow:0 20px 60px rgba(0,0,0,0.6)}
.modal-overlay.open .modal{transform:scale(1)}
.modal-title{font-size:1rem;font-weight:700;margin-bottom:8px}
.modal-desc{font-size:0.82rem;color:var(--accent);margin-bottom:24px;line-height:1.5}
.modal-btns{display:flex;gap:10px}
.modal-cancel{flex:1;padding:12px;background:var(--elem);color:var(--accent);border:none;border-radius:10px;font-size:0.9rem;font-weight:600;cursor:pointer}
.modal-ok{flex:1;padding:12px;border:none;border-radius:10px;font-size:0.9rem;font-weight:600;cursor:pointer}
.modal-ok-yellow{background:var(--a);color:var(--bg)}
.modal-ok-red{background:var(--b);color:#fff}
.win-selector{display:flex;align-items:center;gap:8px;margin-bottom:16px}
.win-label{font-size:0.65rem;letter-spacing:1px;text-transform:uppercase;color:var(--accent);flex:1}
.win-btn{flex:1;border:none;border-radius:8px;padding:8px 4px;font-size:0.95rem;font-weight:700;cursor:pointer;background:var(--elem);color:var(--accent);transition:all .2s;-webkit-tap-highlight-color:transparent}
.win-btn.active{background:var(--a);color:var(--bg)}
.btn-icon{background:none;border:none;color:var(--accent);cursor:pointer;padding:4px;display:flex;align-items:center;border-radius:6px;-webkit-tap-highlight-color:transparent}
.btn-icon:active{opacity:0.6}
.page{display:none}.page.active{display:block}
.page-header{display:flex;align-items:center;gap:12px;margin-bottom:24px}
.page-title{font-size:0.8rem;letter-spacing:3px;text-transform:uppercase;color:var(--accent);font-weight:600;flex:1;text-align:center}
.btn-back{background:none;border:none;color:var(--accent);cursor:pointer;font-size:0.85rem;font-weight:600;display:flex;align-items:center;gap:5px;padding:4px 0;-webkit-tap-highlight-color:transparent}
.btn-back:active{opacity:0.6}
@keyframes noticeIn{from{opacity:0;transform:translateY(-6px)}to{opacity:1;transform:translateY(0)}}
.rotate-notice{background:rgba(var(--accent-rgb),0.12);color:var(--accent);border:1px solid rgba(var(--accent-rgb),0.25);border-radius:10px;padding:10px 16px;text-align:center;font-size:0.82rem;font-weight:600;letter-spacing:1px;margin-bottom:12px;display:none}
.rotate-notice.visible{display:block;animation:noticeIn .3s ease}
.timer-val{font-size:3rem;font-weight:700;font-variant-numeric:tabular-nums;color:var(--accent);letter-spacing:6px;line-height:1}
.timer-sub{font-size:0.55rem;letter-spacing:3px;text-transform:uppercase;color:var(--accent);opacity:0.5;margin-top:6px}
.scores.timer-active .set-badge{top:6px;transform:none}
.bri-compact{background:var(--card);border-radius:16px;padding:8px 16px 10px;margin-bottom:10px}
.bri-compact-row{display:flex;justify-content:space-between;font-size:0.6rem;letter-spacing:1px;text-transform:uppercase;color:var(--accent);margin-bottom:5px;opacity:0.7}
.slider-sm{height:16px;margin:0}
.slider-sm::-webkit-slider-thumb{-webkit-appearance:none;width:14px;height:14px;border-radius:50%;background:var(--a);cursor:pointer;box-shadow:0 1px 6px rgba(0,0,0,0.5)}
.slider-sm::-moz-range-thumb{width:14px;height:14px;border-radius:50%;background:var(--a);cursor:pointer;border:none}
.srv-corner{font-size:0.65rem;font-weight:700;padding:3px 8px;border-radius:5px;cursor:pointer;border:1px solid;opacity:.2;transition:opacity .2s,background .2s;user-select:none;-webkit-tap-highlight-color:transparent;white-space:nowrap}
.srv-corner-a{color:var(--a);border-color:rgba(var(--a-rgb),.3)}
.srv-corner-b{color:var(--b);border-color:rgba(var(--b-rgb),.3)}
.srv-corner.active-a{opacity:1;background:rgba(var(--a-rgb),.18)}
.srv-corner.active-b{opacity:1;background:rgba(var(--b-rgb),.18)}
.team-row{display:flex;align-items:center;justify-content:center;gap:6px;margin-bottom:10px}
.team-name{font-size:0.65rem;letter-spacing:2px;text-transform:uppercase;color:var(--accent)}
.collapsible-summary{display:flex;flex-direction:column;align-items:center;gap:3px;cursor:pointer;list-style:none;outline:none;-webkit-tap-highlight-color:transparent;user-select:none}
.collapsible-summary:focus{outline:none}
.collapsible-summary::-webkit-details-marker{display:none}
.collapsible-summary::after{content:'▾';font-size:0.9rem;font-weight:400;transition:transform .2s}
details[open] .collapsible-summary::after{transform:rotate(-180deg)}
.collapsible-body{margin-top:14px}
.wifi-toggle{position:relative;display:inline-flex;align-items:center;width:68px;height:28px;border-radius:14px;background:rgba(255,255,255,0.12);cursor:pointer;user-select:none;-webkit-tap-highlight-color:transparent;transition:background .22s;flex-shrink:0}
.wifi-toggle[data-enabled="1"]{background:var(--accent)}
.wt-knob{position:absolute;left:3px;width:22px;height:22px;border-radius:50%;background:#fff;box-shadow:0 1px 5px rgba(0,0,0,.4);transition:left .22s;pointer-events:none;z-index:1}
.wifi-toggle[data-enabled="1"] .wt-knob{left:43px}
.wt-off,.wt-on{position:absolute;font-size:0.58rem;font-weight:700;letter-spacing:.5px;pointer-events:none;transition:opacity .15s}
.wt-off{right:9px;color:rgba(255,255,255,0.5)}
.wt-on{left:9px;color:#fff;opacity:0}
.wifi-toggle[data-enabled="1"] .wt-off{opacity:0}
.wifi-toggle[data-enabled="1"] .wt-on{opacity:1}
</style>
</head>
<body>
<div class="container">
<div id="pageMain" class="page active">
  <div class="header">
    <div style="display:flex;align-items:center;justify-content:space-between">
      <div class="logo" id="pageTitle">Roundnet Scoreboard</div>
      <button class="btn-icon" onclick="showPage('pageSettings')" aria-label="Settings"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg></button>
    </div>
    <div id="wifiIndicator" class="wifi-indicator" style="display:none"><svg width="13" height="10" viewBox="0 0 13 10" fill="currentColor" xmlns="http://www.w3.org/2000/svg"><circle cx="6.5" cy="8.5" r="1.2"/><path d="M4 5.8C4.7 5 5.6 4.6 6.5 4.6S8.3 5 9 5.8" stroke="currentColor" stroke-width="1.1" stroke-linecap="round" fill="none"/><path d="M1.5 3C2.9 1.5 4.6.7 6.5.7S10.1 1.5 11.5 3" stroke="currentColor" stroke-width="1.1" stroke-linecap="round" fill="none"/></svg><span id="wifiIndicatorSSID"></span></div>
  </div>

  <div class="bri-compact">
    <div class="bri-compact-row"><span>Brightness</span><span><span id="brightVal">31</span>%</span></div>
    <input type="range" id="brightness" class="slider slider-sm" min="1" max="255" value="80" oninput="setBrightness(this.value)">
  </div>

  <div class="scoreboard">
    <div class="rotate-notice" id="rotateNotice">&#8635; Players rotate positions</div>
    <div class="scores">
      <div class="set-badge set-badge-a" id="setA">0</div>
      <div class="set-badge set-badge-b" id="setB">0</div>
      <div id="scoreView" style="display:contents">
        <div class="team">
          <div class="team-row">
            <div class="srv-corner srv-corner-a active-a" id="srvCornerA" onclick="setFirstServer(0)">1st</div>
            <span class="team-name" id="lblA">Team A</span>
          </div>
          <div class="score-wrap">
            <div class="serve-col serve-col-a" id="serveA"></div>
            <div class="score score-a" id="scoreA">00</div>
          </div>
        </div>
        <div class="divider"></div>
        <div class="team">
          <div class="team-row">
            <span class="team-name" id="lblB">Team B</span>
            <div class="srv-corner srv-corner-b" id="srvCornerB" onclick="setFirstServer(1)">1st</div>
          </div>
          <div class="score-wrap">
            <div class="score score-b" id="scoreB">00</div>
            <div class="serve-col serve-col-b" id="serveB"></div>
          </div>
        </div>
      </div>
      <div id="timerView" style="display:none;text-align:center;width:100%;padding:32px 0 8px">
        <div class="timer-val" id="timerText">3:00</div>
        <div class="timer-sub" id="timerSub">Break</div>
      </div>
    </div>

    <div class="set-history" id="setHistory" style="display:none"></div>
    <div class="ratio-bar"><div class="ratio-a" id="ratioA"></div><div class="ratio-b" id="ratioB"></div></div>

    <div class="controls" id="controls">
      <button class="btn btn-primary-a btn-repeat" onpointerdown="startRepeat('/a/inc',this)" onpointerup="stopRepeat()" onpointerleave="stopRepeat()">+1</button>
      <button class="btn btn-primary-b btn-repeat" onpointerdown="startRepeat('/b/inc',this)" onpointerup="stopRepeat()" onpointerleave="stopRepeat()">+1</button>
      <button class="btn btn-secondary-a" onclick="action('/a/dec',this)">−1</button>
      <button class="btn btn-secondary-b" onclick="action('/b/dec',this)">−1</button>
    </div>

    <div class="actions" id="actions">
      <button class="btn" onclick="openModal('Next Set','Confirm the current set is over and start the next one.','modal-ok-yellow','/nextset')">Next Set</button>
      <button class="btn" onclick="openModal('Reset Score','Reset all scores and sets. This cannot be undone.','modal-ok-red','/reset')">Reset</button>
    </div>
  </div>

  <div class="settings" style="margin-top:12px">
    <div class="setting-group" style="margin:0">
      <button class="btn" style="width:100%" onclick="action('/timeout',this)">Timeout</button>
    </div>
  </div>

  <details class="settings" style="margin-top:12px;padding:0">
    <summary class="setting-label collapsible-summary" style="padding:7px 20px">Game Settings</summary>
    <div class="collapsible-body" style="padding:0 20px 16px">
      <div class="setting-group">
        <label class="setting-label">Win Score</label>
        <div class="win-selector">
          <button class="win-btn" data-wp="11" onclick="setWinPoints(11)">11</button>
          <button class="win-btn" data-wp="15" onclick="setWinPoints(15)">15</button>
          <button class="win-btn" data-wp="17" onclick="setWinPoints(17)">17</button>
          <button class="win-btn active" data-wp="21" onclick="setWinPoints(21)">21</button>
        </div>
        <div style="display:flex;gap:8px;margin-top:8px;align-items:center">
          <input type="number" id="wpCustom" min="1" max="99" placeholder="Custom" class="input" style="flex:1;padding:8px 10px;font-size:0.85rem">
          <button onclick="applyCustomWinPoints()" style="padding:8px 14px;background:var(--accent);color:#fff;border:none;border-radius:8px;font-size:0.85rem;cursor:pointer;white-space:nowrap">Set</button>
        </div>
      </div>
      <div class="setting-group">
        <label class="setting-label">Hardcap</label>
        <div class="win-selector">
          <button class="win-btn" data-hc="15" onclick="setHardcap(15)">15</button>
          <button class="win-btn" data-hc="17" onclick="setHardcap(17)">17</button>
          <button class="win-btn" data-hc="21" onclick="setHardcap(21)">21</button>
          <button class="win-btn" data-hc="25" onclick="setHardcap(25)">25</button>
        </div>
        <div style="display:flex;gap:8px;margin-top:8px;align-items:center">
          <input type="number" id="hcCustom" min="0" max="99" placeholder="Custom (0 = off)" class="input" style="flex:1;padding:8px 10px;font-size:0.85rem">
          <button onclick="applyCustomHardcap()" style="padding:8px 14px;background:var(--accent);color:#fff;border:none;border-radius:8px;font-size:0.85rem;cursor:pointer;white-space:nowrap">Set</button>
        </div>
      </div>
      <div class="setting-group" style="margin-bottom:0">
        <label class="setting-label">Format</label>
        <div class="win-selector">
          <button class="win-btn" data-fmt="0" onclick="setFormat(0)">BO1</button>
          <button class="win-btn" data-fmt="1" onclick="setFormat(1)">2 sets</button>
          <button class="win-btn active" data-fmt="2" onclick="setFormat(2)">BO3</button>
        </div>
      </div>
    </div>
  </details>

  <div class="settings" style="margin-top:12px">
    <div class="setting-group" style="margin:0">
      <button class="btn" style="width:100%;background:rgba(var(--b-rgb),0.1);color:var(--b);border:1px solid rgba(var(--b-rgb),0.3)"
        onclick="openModal('Sleep','Put the scoreboard to sleep?','modal-ok-red','/sleepnow')">Sleep</button>
    </div>
  </div>

  <div class="settings" style="margin-top:12px">
    <div class="setting-group" style="margin:0">
      <a href="/overlay" style="display:block;text-align:center;padding:14px;background:var(--elem);color:var(--accent);border-radius:12px;font-size:0.85rem;font-weight:600;text-decoration:none">&#127916; Video Overlay Export</a>
    </div>
  </div>

</div><!-- /pageMain -->

<div id="pageSettings" class="page">
  <div class="page-header">
    <button class="btn-back" onclick="showPage('pageMain')"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="15 18 9 12 15 6"/></svg> Back</button>
    <span class="page-title">Settings</span>
    <div style="width:40px"></div>
  </div>

  <div class="settings">
    <div class="setting-group">
      <label class="setting-label">Scoreboard Name</label>
      <div style="display:flex;gap:8px">
        <input type="text" class="input" id="boardId" maxlength="31" placeholder="Roundnet Scoreboard" oninput="_boardIdDirty=true">
        <button class="btn" style="background:var(--a);color:var(--bg);padding:12px 16px;font-size:0.85rem;white-space:nowrap;border-radius:8px" onclick="confirmBoardIdSave()">Save</button>
      </div>
      <div style="font-size:0.7rem;color:var(--accent);margin-top:6px;line-height:1.4">WiFi hotspot name and ESP-NOW discovery ID.</div>
    </div>
  </div>

  <div class="settings">
    <div class="setting-group">
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px">
        <label class="setting-label" style="margin:0">Battery Saver</label>
        <button id="sleepBtn" data-sleeping="0" onclick="toggleSleep()" style="padding:6px 12px;background:var(--surface);color:var(--accent);border:1px solid var(--accent);border-radius:8px;font-size:0.8rem;cursor:pointer">Sleep now</button>
      </div>
      <div class="mode-selector">
        <button class="mode-btn" id="ds0"   onclick="setDimSleep(0)">Off<span class="mode-sub">always on</span></button>
        <button class="mode-btn" id="ds300" onclick="setDimSleep(300)">5 min<span class="mode-sub">auto-dim</span></button>
        <button class="mode-btn" id="ds600" onclick="setDimSleep(600)">10 min<span class="mode-sub">auto-dim</span></button>
        <button class="mode-btn" id="ds900" onclick="setDimSleep(900)">15 min<span class="mode-sub">auto-dim</span></button>
      </div>
      <div style="display:flex;gap:8px;margin-top:8px;align-items:center">
        <input type="number" id="dimCustom" min="0" max="3600" placeholder="Custom (seconds)" style="flex:1;padding:8px 10px;background:var(--surface);color:var(--text);border:1px solid var(--border);border-radius:8px;font-size:0.85rem">
        <button onclick="applyCustomDim()" style="padding:8px 14px;background:var(--accent);color:var(--bg);border:none;border-radius:8px;font-size:0.85rem;cursor:pointer;white-space:nowrap">Set</button>
      </div>
      <div style="font-size:0.7rem;color:var(--accent);margin-top:6px;line-height:1.4">After inactivity, switches to a 2-dot breathing animation to save battery. Any score action wakes the display.</div>
    </div>
  </div>

  <div class="settings" id="networkModeSection">
    <div class="setting-group">
      <label class="setting-label">Network Mode</label>
      <div class="mode-selector">
        <button class="mode-btn" id="modeLocal" onclick="setMode('local')">Local<span class="mode-sub">standalone</span></button>
        <button class="mode-btn" id="modeCentral" onclick="setMode('central')">Central<span class="mode-sub">server sync</span></button>
        <button class="mode-btn" id="modeFirebase" onclick="setMode('firebase')">Firebase<span class="mode-sub">cloud sync</span></button>
      </div>
      <div class="mode-hint" id="modeHint" style="margin-top:8px"></div>
    </div>
  </div>

  <div class="settings">
    <div class="setting-group" id="wifiGroup">
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px">
        <label class="setting-label" style="margin:0">WiFi <span id="wifiStatus" class="status-badge status-offline">Offline</span></label>
        <div class="wifi-toggle" id="wifiToggleBtn" data-enabled="0" onclick="toggleWifi()"><span class="wt-on">ON</span><span class="wt-knob"></span><span class="wt-off">OFF</span></div>
      </div>
      <div id="wifiControls" style="display:none">
        <button class="btn btn-scan" id="btnScan" onclick="scanWiFi()">Scan Networks</button>
        <div class="network-list" id="networkList" style="display:none"></div>
        <button class="btn-disconnect" id="btnDisconnect" style="display:none" onclick="disconnectWiFi()">Disconnect from <span id="connectedSSID"></span></button>
      </div>
    </div>

    <div class="setting-group" id="centralSection">
      <label class="setting-label">Central Server <span id="wsStatus" class="status-badge status-offline">Disconnected</span></label>
      <div style="display:flex;gap:8px">
        <input type="text" class="input" id="serverIp" placeholder="e.g. 192.168.1.100" oninput="_serverIpDirty=true">
        <button class="btn" style="background:var(--a);color:var(--bg);padding:12px 16px;font-size:0.85rem;white-space:nowrap;border-radius:8px" onclick="saveServerIp()">Save</button>
      </div>
      <div style="font-size:0.7rem;color:var(--accent);margin-top:6px;line-height:1.4">WebSocket server IP for central scoreboard management. Leave empty to disable.</div>
    </div>

    <div class="setting-group" id="firebaseSection" style="display:none">
      <label class="setting-label">Firebase</label>
      <div style="margin-bottom:12px">
        <div style="font-size:0.7rem;color:var(--accent);margin-bottom:6px">Channel</div>
        <select class="input" id="fbChannel" onchange="saveFbChannel()">
          <option value="">— select channel —</option>
        </select>
      </div>
      <div>
        <div style="font-size:0.7rem;color:var(--accent);margin-bottom:6px">Poll interval (seconds)</div>
        <div style="display:flex;gap:8px">
          <input type="number" class="input" id="fbPoll" min="1" max="60" placeholder="3">
          <button class="btn" style="background:var(--elem);color:var(--accent);padding:12px 16px;font-size:0.85rem;white-space:nowrap;border-radius:8px" onclick="saveFbPoll()">Set</button>
        </div>
      </div>
    </div>
  </div>

  <div class="settings" style="margin-top:12px">
    <div class="setting-group" style="margin:0">
      <a href="/update" style="display:block;text-align:center;padding:14px;background:var(--elem);color:var(--accent);border-radius:12px;font-size:0.85rem;font-weight:600;text-decoration:none">Update Firmware</a>
    </div>
  </div>

</div><!-- /pageSettings -->
</div><!-- /container -->

<div class="modal-overlay" id="modalOverlay" onclick="if(event.target===this)closeModal()">
  <div class="modal">
    <div class="modal-title" id="modalTitle"></div>
    <div class="modal-desc" id="modalDesc"></div>
    <div class="modal-btns">
      <button class="modal-cancel" onclick="closeModal()">Cancel</button>
      <button class="modal-ok" id="modalOk" onclick="confirmModal()">Confirm</button>
    </div>
  </div>
</div>

<script>
let _toastTimer = null;
function showToast(msg) {
  if (_toastTimer) { clearTimeout(_toastTimer); document.querySelector('.toast')?.remove(); }
  const t = document.createElement('div');
  t.className = 'toast';
  t.textContent = msg;
  document.querySelector('.scoreboard').appendChild(t);
  _toastTimer = setTimeout(() => { t.remove(); _toastTimer = null; }, 2500);
}

let _isConnecting = false;
let _repeatTimer = null, _repeatStart = null;
let _pendingAction = null;
let _pendingCallback = null;
let _lastRotations = -1;
let _rotateNoticeTimer = null;
let _localTimerInterval = null;
let _localTimerEndMs = 0;

function showRotationNotice() {
  if (_rotateNoticeTimer) clearTimeout(_rotateNoticeTimer);
  const el = document.getElementById('rotateNotice');
  el.classList.remove('visible');
  void el.offsetWidth;
  el.classList.add('visible');
  _rotateNoticeTimer = setTimeout(() => el.classList.remove('visible'), 4000);
}

function _showTimerView(label) {
  document.getElementById('timerSub').textContent = label;
  if (!_localTimerInterval) {
    document.getElementById('scoreView').style.display = 'none';
    document.getElementById('timerView').style.display = 'block';
    document.querySelector('.scores').classList.add('timer-active');
    _localTimerInterval = setInterval(updateTimerDisplay, 200);
  }
}

function _hideTimerView() {
  if (!_localTimerInterval) return;
  clearInterval(_localTimerInterval); _localTimerInterval = null;
  document.getElementById('scoreView').style.display = 'contents';
  document.getElementById('timerView').style.display = 'none';
  document.querySelector('.scores').classList.remove('timer-active');
}

function updateTimerDisplay() {
  const remaining = Math.max(0, _localTimerEndMs - Date.now());
  const totalSec = Math.ceil(remaining / 1000);
  const m = Math.floor(totalSec / 60);
  const s = totalSec % 60;
  document.getElementById('timerText').textContent = m + ':' + String(s).padStart(2, '0');
  if (remaining <= 0) _hideTimerView();
}

function openModal(title, desc, okClass, urlOrCb, okLabel) {
  if (typeof urlOrCb === 'function') { _pendingCallback = urlOrCb; _pendingAction = null; }
  else { _pendingAction = urlOrCb; _pendingCallback = null; }
  document.getElementById('modalTitle').textContent = title;
  document.getElementById('modalDesc').textContent = desc;
  const ok = document.getElementById('modalOk');
  ok.className = 'modal-ok ' + okClass;
  ok.textContent = okLabel || title;
  document.getElementById('modalOverlay').classList.add('open');
}
function closeModal() {
  document.getElementById('modalOverlay').classList.remove('open');
  _pendingAction = null; _pendingCallback = null;
}
async function confirmModal() {
  if (_pendingCallback) { const cb = _pendingCallback; closeModal(); await cb(); return; }
  const url = _pendingAction;
  closeModal();
  if (url) await action(url);
}

function confirmBoardIdSave() {
  const val = document.getElementById('boardId').value.trim();
  if (val.length === 0) return;
  openModal(
    'Rename Scoreboard',
    'Your scoreboard’s Wi‑Fi hotspot will be renamed to «' + val + '». You’ll be disconnected — to get back, open your Wi‑Fi settings and connect to «' + val + '».',
    'modal-ok-yellow',
    saveBoardId,
    'Rename'
  );
}

async function action(url, btn) {
  if (btn) { btn.classList.add('flash'); setTimeout(() => btn.classList.remove('flash'), 150); }
  try {
    const r = await fetch(url, {method:'POST'});
    if (r.status === 409) { showToast('Teams are even — no winner yet'); return; }
    refresh();
  } catch(e) {}
}

function startRepeat(url, btn) {
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

    const mkDot = (cls) => { const e = document.createElement('div'); e.className = 'serve-dot ' + cls; return e; };
    const sa = document.getElementById('serveA'); sa.innerHTML = '';
    const sb = document.getElementById('serveB'); sb.innerHTML = '';
    const setWon = Math.max(d.scoreA, d.scoreB) >= d.winPoints && Math.abs(d.scoreA - d.scoreB) >= 2;
    if (!setWon) {
      const dotCls = d.serving === 0 ? 'serve-dot-a' : 'serve-dot-b';
      const serveCol = d.serving === 0 ? sa : sb;
      if (d.serveTotal === 2) {
        serveCol.appendChild(mkDot(dotCls + (d.servesLeft < 2 ? ' serve-dot-dim' : '')));
        serveCol.appendChild(mkDot(dotCls));
      } else {
        serveCol.appendChild(mkDot(dotCls));
      }
    }
    document.getElementById('srvCornerA').className = 'srv-corner srv-corner-a' + (d.firstServer === 0 ? ' active-a' : '');
    document.getElementById('srvCornerB').className = 'srv-corner srv-corner-b' + (d.firstServer === 1 ? ' active-b' : '');

    if (d.boardId && !_boardIdDirty) {
      document.getElementById('boardId').value = d.boardId;
      document.getElementById('pageTitle').textContent = d.boardId;
    }
    if (d.winPoints) {
      document.querySelectorAll('[data-wp]').forEach(b =>
        b.classList.toggle('active', parseInt(b.dataset.wp) === d.winPoints));
      const wpPresets = [11, 15, 17, 21];
      document.getElementById('wpCustom').value = wpPresets.includes(d.winPoints) ? '' : d.winPoints;
    }
    if (d.hardcap !== undefined) {
      document.querySelectorAll('[data-hc]').forEach(b =>
        b.classList.toggle('active', parseInt(b.dataset.hc) === d.hardcap));
      const hcPresets = [15, 17, 21, 25];
      document.getElementById('hcCustom').value = (d.hardcap === 0 || hcPresets.includes(d.hardcap)) ? '' : d.hardcap;
    }
    if (d.format !== undefined) {
      document.querySelectorAll('[data-fmt]').forEach(b =>
        b.classList.toggle('active', parseInt(b.dataset.fmt) === d.format));
    }

    if (d.dimSleep !== undefined) {
      document.querySelectorAll('[id^="ds"]').forEach(b =>
        b.classList.toggle('active', b.id === 'ds' + d.dimSleep));
    }
    if (d.sleeping !== undefined) _updateSleepBtn(d.sleeping);
    if (d.serverIp !== undefined && !_serverIpDirty) document.getElementById('serverIp').value = d.serverIp;
    if (d.wsConnected !== undefined) {
      const ws = document.getElementById('wsStatus');
      ws.className = 'status-badge ' + (d.wsConnected ? 'status-online' : 'status-offline');
      ws.textContent = d.wsConnected ? 'Connected' : 'Disconnected';
    }
    const hist = document.getElementById('setHistory');
    if (d.setsPlayed > 0 && d.histA && d.histB) {
      hist.style.display = 'flex';
      hist.innerHTML = '';
      for (let i = 0; i < d.setsPlayed; i++) {
        const aWon = d.histA[i] > d.histB[i];
        const chip = document.createElement('div');
        chip.className = 'set-chip';
        chip.innerHTML = '<span class="set-chip-lbl">S'+(i+1)+'</span>'
          +'<span class="set-chip-score'+(aWon?' win-a':'')+'">'+d.histA[i]+'</span>'
          +'<span class="set-chip-sep">–</span>'
          +'<span class="set-chip-score'+(!aWon?' win-b':'')+'">'+d.histB[i]+'</span>';
        hist.appendChild(chip);
      }
    } else {
      hist.style.display = 'none';
    }

    // Rotation notice
    if (d.rotations !== undefined) {
      if (_lastRotations >= 0 && d.rotations > _lastRotations) showRotationNotice();
      _lastRotations = d.rotations;
    }

    // Break timer / timeout display
    const breakSecs = d.breakTimer   || 0;
    const toSecs    = d.timeoutTimer || 0;

    if (toSecs > 0) {
      _showTimerView('Time Out');
      _localTimerEndMs = Date.now() + toSecs * 1000;
      updateTimerDisplay();
    } else if (breakSecs > 0) {
      _showTimerView('Break');
      _localTimerEndMs = Date.now() + breakSecs * 1000;
      updateTimerDisplay();
    } else {
      _hideTimerView();
    }

    if (d.brightness !== undefined && !_brightnessDirty) {
      document.getElementById('brightness').value = d.brightness;
      document.getElementById('brightVal').textContent = Math.max(1, Math.round(d.brightness / 255 * 100));
    }

    const indicator = document.getElementById('wifiIndicator');
    const status = document.getElementById('wifiStatus');
    const btnDisc = document.getElementById('btnDisconnect');
    if (d.online && d.ssid) {
      _isConnecting = false;
      indicator.style.display = 'flex';
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

    const nmSection = document.getElementById('networkModeSection');
    if (nmSection) nmSection.style.display = (d.online && d.ssid) ? '' : 'none';

    const wifiToggle = document.getElementById('wifiToggleBtn');
    if (wifiToggle && d.wifiEnabled !== undefined) {
      const en = d.wifiEnabled;
      wifiToggle.dataset.enabled = en ? '1' : '0';
      const wc = document.getElementById('wifiControls');
      if (wc) wc.style.display = en ? 'block' : 'none';
    }

    if (d.mode !== undefined) _applyModeUI(d.mode);
    const _fbCh = document.getElementById('fbChannel');
    if (_fbCh && d.fbChannel !== undefined) _fbCh.value = d.fbChannel;
    const _fbPo = document.getElementById('fbPoll');
    if (_fbPo && d.fbPoll !== undefined) _fbPo.value = d.fbPoll;

  } catch(e) {}
}

const _HC_FOR_WIN = {11:15, 15:17, 17:21, 21:25};

async function setWinPoints(val) {
  document.querySelectorAll('[data-wp]').forEach(b =>
    b.classList.toggle('active', parseInt(b.dataset.wp) === val));
  try { await fetch('/winpoints', {method:'POST', body: String(val)}); } catch(e) {}
  // Auto-set matching hardcap for preset values
  if (_HC_FOR_WIN[val] !== undefined) setHardcap(_HC_FOR_WIN[val]);
}
function applyCustomWinPoints() {
  const v = parseInt(document.getElementById('wpCustom').value);
  if (!isNaN(v) && v >= 1 && v <= 99) {
    // Custom win score: only set win points, leave hardcap unchanged
    document.querySelectorAll('[data-wp]').forEach(b => b.classList.remove('active'));
    fetch('/winpoints', {method:'POST', body: String(v)}).catch(()=>{});
  }
}
async function setHardcap(val) {
  document.querySelectorAll('[data-hc]').forEach(b =>
    b.classList.toggle('active', parseInt(b.dataset.hc) === val));
  try { await fetch('/hardcap', {method:'POST', body: String(val)}); } catch(e) {}
}
function applyCustomHardcap() {
  const v = parseInt(document.getElementById('hcCustom').value);
  if (!isNaN(v) && v >= 0 && v <= 99) setHardcap(v);
}
async function setFormat(val) {
  document.querySelectorAll('[data-fmt]').forEach(b =>
    b.classList.toggle('active', parseInt(b.dataset.fmt) === val));
  try { await fetch('/format', {method:'POST', body: String(val)}); } catch(e) {}
}

function showPage(id) {
  document.querySelectorAll('.page').forEach(p => p.classList.toggle('active', p.id === id));
  document.body.classList.toggle('top-align', id === 'pageSettings');
}

async function setDimSleep(secs) {
  document.querySelectorAll('[id^="ds"]').forEach(b =>
    b.classList.toggle('active', b.id === 'ds' + secs));
  try { await fetch('/dimsleep', {method:'POST', body: String(secs)}); } catch(e) {}
}

function applyCustomDim() {
  const v = parseInt(document.getElementById('dimCustom').value);
  if (!isNaN(v) && v >= 0) setDimSleep(v);
}

function _updateSleepBtn(sleeping) {
  const btn = document.getElementById('sleepBtn');
  if (!btn) return;
  btn.dataset.sleeping = sleeping ? '1' : '0';
  btn.textContent = sleeping ? 'Wake up' : 'Sleep now';
  btn.style.background = sleeping ? 'var(--accent)' : 'var(--surface)';
  btn.style.color      = sleeping ? 'var(--bg)'     : 'var(--accent)';
}

async function toggleSleep() {
  const sleeping = document.getElementById('sleepBtn').dataset.sleeping === '1';
  _updateSleepBtn(!sleeping);  // optimistic update
  try { await fetch(sleeping ? '/wake' : '/sleepnow', {method:'POST'}); } catch(e) { refresh(); }
}

async function setFirstServer(who) {
  try { await fetch('/serve/first', {method:'POST', body: String(who)}); refresh(); } catch(e) {}
}

async function scanWiFi() {
  const btn = document.getElementById('btnScan');
  btn.disabled = true;
  btn.innerHTML = '<div class="spinner"></div>Scanning\u2026';
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
  btn.innerHTML = 'Scan Networks';
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
let _boardIdDirty = false;
let _serverIpDirty = false;
async function saveBoardId() {
  const val = document.getElementById('boardId').value.trim();
  if (val.length === 0) return;
  try {
    await fetch('/setboardid', {method:'POST', body: val});
    document.getElementById('pageTitle').textContent = val;
    _boardIdDirty = false;
  } catch(e) {}
}
function setBrightness(val) {
  document.getElementById('brightVal').textContent = Math.max(1, Math.round(val / 255 * 100));
  _brightnessDirty = true;
  clearTimeout(_brightnessTimer);
  _brightnessTimer = setTimeout(async () => {
    try { await fetch('/brightness', {method:'POST', body: val}); } catch(e) {}
    _brightnessDirty = false;
  }, 300);
}

async function saveServerIp() {
  const val = document.getElementById('serverIp').value.trim();
  try { await fetch('/serverip', {method:'POST', body: val}); _serverIpDirty = false; } catch(e) {}
}

const _MODE_HINTS = {
  local: 'Standalone — no network sync. Scores are controlled locally via portal or pedal.',
  central: 'Syncs with the central scoreboard manager server on your local network.',
  firebase: 'Syncs directly with Firebase Realtime Database for cloud-based scoring.'
};

function _applyModeUI(m) {
  ['Local','Central','Firebase'].forEach(n => {
    document.getElementById('mode'+n)?.classList.toggle('active', m === n.toLowerCase());
  });
  const hint = document.getElementById('modeHint');
  if (hint) hint.textContent = _MODE_HINTS[m] || '';
  const cs = document.getElementById('centralSection');
  const fs = document.getElementById('firebaseSection');
  if (cs) cs.style.display = m === 'central' ? '' : 'none';
  if (fs) fs.style.display = m === 'firebase' ? '' : 'none';
}

async function setMode(m) {
  try { await fetch('/mode', {method:'POST', body: m}); refresh(); } catch(e) {}
}

// Populate channel select 1-23
(function(){
  const s = document.getElementById('fbChannel');
  if (!s) return;
  for (let i = 1; i <= 23; i++) {
    const o = document.createElement('option');
    o.value = String(i); o.textContent = 'Channel ' + i;
    s.appendChild(o);
  }
})();

async function saveFbChannel() {
  const val = document.getElementById('fbChannel')?.value;
  if (!val) return;
  try { await fetch('/firebase/channel', {method:'POST', body: val}); } catch(e) {}
}

async function saveFbPoll() {
  const val = parseInt(document.getElementById('fbPoll')?.value);
  if (isNaN(val) || val < 1) return;
  try { await fetch('/firebase/pollinterval', {method:'POST', body: String(val)}); } catch(e) {}
}

async function toggleWifi() {
  const btn = document.getElementById('wifiToggleBtn');
  const isEnabled = btn.dataset.enabled === '1';
  await fetch(isEnabled ? '/wifi/disable' : '/wifi/enable', {method:'POST'}).catch(()=>{});
  await refresh();
  if (!isEnabled) scanWiFi();
}

setInterval(refresh, 2000);
document.addEventListener('visibilitychange', () => { if (!document.hidden) refresh(); });
refresh();
</script>
</body>
</html>
)rawhtml";

static const char OTA_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Firmware Update</title>
<style>
:root{--bg:#1c1830;--card:#251f40;--elem:#3a3460;--border:#4a4478;--a:#f5c518;--b:#e83e8c;--accent:#8070a8}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:var(--bg);color:#fff;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}
.wrap{width:100%;max-width:360px}
.hdr{text-align:center;margin-bottom:24px}
.logo{font-size:.65rem;letter-spacing:4px;text-transform:uppercase;color:var(--accent);margin-bottom:6px;font-weight:600}
h1{font-size:1.1rem;font-weight:700;margin-bottom:4px}
.cur-ver{font-size:.75rem;color:var(--accent);margin-top:4px}
.card{background:var(--card);border-radius:16px;padding:20px;margin-bottom:12px}
.lbl{font-size:.68rem;letter-spacing:1px;text-transform:uppercase;color:var(--accent);margin-bottom:10px}
.btn{width:100%;padding:13px;border:none;border-radius:10px;font-size:.9rem;font-weight:600;cursor:pointer;background:var(--a);color:#1c1830}
.btn:disabled{opacity:.5;cursor:default}
.btn-sec{background:var(--elem);color:var(--accent)}
.result{margin-top:12px;background:var(--elem);border-radius:10px;padding:12px}
.result-row{display:flex;justify-content:space-between;font-size:.82rem;margin-bottom:4px}
.rlbl{color:var(--accent);font-size:.68rem;letter-spacing:1px;text-transform:uppercase}
.notes{font-size:.75rem;color:var(--accent);margin-top:6px;line-height:1.4}
.ok{color:#66dd66;font-size:.82rem;text-align:center;margin-top:8px}
input[type=file]{width:100%;background:var(--elem);border:1px solid var(--border);border-radius:8px;color:#fff;padding:10px;font-size:.85rem;cursor:pointer;margin-bottom:10px}
.prog{display:none;margin-top:14px}
.bar-bg{background:var(--elem);border-radius:4px;height:8px;overflow:hidden}
.bar-fg{height:100%;background:var(--a);border-radius:4px;width:0;transition:width .2s}
.st{font-size:.78rem;color:var(--accent);margin-top:6px;text-align:center}
a.back{display:block;text-align:center;margin-top:14px;color:var(--accent);font-size:.82rem;text-decoration:none}
</style>
</head>
<body>
<div class="wrap">
<div class="hdr">
  <div class="logo">Roundnet Scoreboard</div>
  <h1>Firmware Update</h1>
  <div class="cur-ver">Current: <span id="cur">…</span></div>
</div>

<div class="card">
  <div class="lbl">Check for Updates</div>
  <button id="btn-check" class="btn btn-sec" onclick="checkUpdate()">Check Now</button>
  <div id="check-res"></div>
</div>

<div class="card">
  <div class="lbl">Manual Upload (.bin)</div>
  <input type="file" id="f" accept=".bin">
  <button id="btn-up" class="btn" onclick="go()">Upload &amp; Flash</button>
  <div class="prog" id="prog">
    <div class="bar-bg"><div class="bar-fg" id="bar"></div></div>
    <div class="st" id="st">Uploading...</div>
  </div>
</div>
<a href="/" class="back">&#8592; Back to Portal</a>
</div>

<script>
fetch('/status').then(r=>r.json()).then(d=>{
  document.getElementById('cur').textContent='v'+(d.version||'?');
}).catch(()=>{ document.getElementById('cur').textContent='?'; });

function checkUpdate(){
  var btn=document.getElementById('btn-check'),res=document.getElementById('check-res');
  btn.disabled=true; btn.textContent='Checking…';
  fetch('/update/check',{method:'POST'}).then(r=>r.json()).then(d=>{
    if(d.error){res.innerHTML='<div class="st" style="color:var(--b)">'+d.error+'</div>';return;}
    var upToDate=d.latest===d.current;
    res.innerHTML='<div class="result">'
      +'<div class="result-row"><span class="rlbl">Latest</span><span>v'+d.latest+'</span></div>'
      +(d.notes?'<div class="notes">'+d.notes+'</div>':'')
      +(upToDate
        ?'<div class="ok">✓ Up to date</div>'
        :'<button class="btn" style="margin-top:12px" id="btn-apply" onclick="applyRemote(\''+d.url.replace(/\\/g,'\\\\').replace(/'/g,"\\'")+'\')" >Install v'+d.latest+'</button>'
         +'<div class="st" id="apply-st"></div>')
      +'</div>';
  }).catch(function(e){
    res.innerHTML='<div class="st" style="color:var(--b)">'+e.message+'</div>';
  }).finally(function(){
    btn.disabled=false; btn.textContent='Check Now';
  });
}

function applyRemote(url){
  var applyBtn=document.getElementById('btn-apply'),st=document.getElementById('apply-st');
  if(applyBtn) applyBtn.disabled=true;
  if(st) st.textContent='Starting download… Board will reboot shortly.';
  fetch('/update/fetch',{method:'POST',headers:{'Content-Type':'text/plain'},body:url})
    .then(r=>r.json()).then(function(d){
      if(st) st.textContent=d.ok?'Downloading and flashing… Board will reboot.':'Error: '+(d.error||'unknown');
    }).catch(function(e){ if(st) st.textContent='Error: '+e.message; });
}

function go(){
  var f=document.getElementById('f').files[0];
  if(!f){alert('Select a .bin file');return;}
  var btn=document.getElementById('btn-up'),prog=document.getElementById('prog'),
      bar=document.getElementById('bar'),st=document.getElementById('st');
  btn.disabled=true; prog.style.display='block';
  var x=new XMLHttpRequest();
  x.upload.onprogress=function(e){
    if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);
      bar.style.width=p+'%'; st.textContent='Uploading... '+p+'%';}
  };
  x.onload=function(){
    if(x.status===200&&x.responseText==='OK'){
      bar.style.width='100%'; st.textContent='Done! Rebooting…';}
    else{st.textContent='Error: '+x.responseText; btn.disabled=false;}
  };
  x.onerror=function(){st.textContent='Upload failed'; btn.disabled=false;};
  var d=new FormData(); d.append('firmware',f);
  x.open('POST','/update'); x.send(d);
}
</script>
</body>
</html>
)rawhtml";

static const char OVERLAY_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Overlay Export</title>
<style>
:root{--bg:#1c1830;--card:#251f40;--elem:#3a3460;--border:#4a4478;--accent:#8070a8;--a:#f5c518;--b:#e83e8c;--a-rgb:245,197,24;--b-rgb:232,62,140}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:var(--bg);color:#fff;min-height:100vh;padding:16px 16px 40px}
.card{background:var(--card);border-radius:16px;padding:16px;margin-bottom:12px}
.lbl{font-size:.65rem;letter-spacing:1px;text-transform:uppercase;color:var(--accent);margin-bottom:8px;display:block}
.inp{width:100%;background:var(--elem);border:1px solid var(--border);border-radius:8px;color:#fff;padding:10px 12px;font-size:.9rem;outline:none;margin-bottom:8px}
.inp:focus{border-color:var(--a)}
select.inp{-webkit-appearance:none;appearance:none;background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='8'%3E%3Cpath d='M1 1l5 5 5-5' stroke='%238070a8' stroke-width='1.5' fill='none' stroke-linecap='round'/%3E%3C/svg%3E");background-repeat:no-repeat;background-position:right 12px center;padding-right:32px}
select.inp option{background:var(--elem)}
.two{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:8px}
.two .inp{margin-bottom:0}
.btn{width:100%;border:none;border-radius:10px;padding:14px;font-size:.9rem;font-weight:600;cursor:pointer;margin-bottom:8px;-webkit-tap-highlight-color:transparent;user-select:none;touch-action:manipulation}
.btn-y{background:var(--a);color:var(--bg)}
.btn-d{background:var(--elem);color:var(--accent)}
.btn-r{background:rgba(232,62,140,.12);color:var(--b);border:1px solid rgba(232,62,140,.3)}
.btn:disabled{opacity:.35;cursor:default}
.two-btn{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:8px}
.two-btn .btn{margin:0}
.hdr{display:flex;align-items:center;margin-bottom:16px}
.back{background:none;border:none;color:var(--accent);cursor:pointer;font-size:.85rem;font-weight:600;display:flex;align-items:center;gap:4px;padding:4px 0;-webkit-tap-highlight-color:transparent}
.ttl{font-size:.75rem;letter-spacing:3px;text-transform:uppercase;color:var(--accent);font-weight:600;flex:1;text-align:center}
.gw{background:#2a2d31;border-radius:8px;padding:0;margin-bottom:10px;line-height:0}
canvas{display:block;width:100%}
video{width:100%;border-radius:8px;background:#000;display:none;margin-bottom:8px}
.st{font-size:.78rem;line-height:1.5;padding:10px 12px;border-radius:8px;margin-bottom:8px}
.st-n{background:var(--elem);color:var(--accent)}
.st-ok{background:rgba(110,231,183,.08);color:#6ee7b7;border:1px solid rgba(110,231,183,.2)}
.st-err{background:rgba(232,62,140,.08);color:var(--b);border:1px solid rgba(232,62,140,.2)}
.dl{display:none;text-align:center;padding:14px;background:rgba(245,197,24,.1);color:var(--a);border:1px solid rgba(245,197,24,.3);border-radius:10px;font-size:.9rem;font-weight:600;text-decoration:none;margin-bottom:8px}
.dl.show{display:block}
.hint{font-size:.78rem;color:var(--accent);line-height:1.5;margin-bottom:10px}
</style>
</head>
<body>
<div class="hdr">
  <button class="back" onclick="location.href='/'">&#8592; Back</button>
  <span class="ttl">Overlay Export</span>
  <div style="width:60px"></div>
</div>

<div class="card">
  <span class="lbl">Match Info</span>
  <input class="inp" id="tournament" placeholder="Tournament (e.g. TSN #3)" oninput="redraw()">
  <input class="inp" id="location"   placeholder="Location (e.g. Mulhouse)"  oninput="redraw()">
  <div class="two">
    <input class="inp" id="tA" placeholder="Team A" oninput="redraw()">
    <input class="inp" id="tB" placeholder="Team B" oninput="redraw()">
  </div>
  <select class="inp" id="rnd" onchange="redraw()">
    <option>Pool Play</option>
    <option>RO32</option>
    <option>RO16</option>
    <option>RO8</option>
    <option>Quarterfinals</option>
    <option>Semifinals</option>
    <option>Finals</option>
  </select>
</div>

<div class="card">
  <span class="lbl">Preview</span>
  <div class="gw"><canvas id="cv" width="500" height="180" style="width:100%;display:block"></canvas></div>
  <div id="logSt" class="st st-n">Loading score log&#8230;</div>
  <button class="btn btn-d" onclick="exportPNG()">&#8595; Download PNG Snapshot</button>
</div>

<div class="card">
  <span class="lbl">Export Overlay Video</span>
  <p class="hint">In your game video, measure the time from when the game started (score 0&#8211;0 visible) to when the first point was scored. Enter it below.</p>
  <span class="lbl" style="margin-top:4px">Time from game start to first point</span>
  <div class="two" style="margin-bottom:8px">
    <div style="position:relative">
      <input type="number" class="inp" id="syncMm" min="0" max="99" value="0" style="margin:0;text-align:center" oninput="updateDurInfo()">
      <span style="position:absolute;right:10px;top:50%;transform:translateY(-50%);font-size:.7rem;color:var(--accent);pointer-events:none">min</span>
    </div>
    <div style="position:relative">
      <input type="number" class="inp" id="syncSs" min="0" max="59" value="0" style="margin:0;text-align:center" oninput="updateDurInfo()">
      <span style="position:absolute;right:10px;top:50%;transform:translateY(-50%);font-size:.7rem;color:var(--accent);pointer-events:none">sec</span>
    </div>
  </div>
  <div id="durInfo" class="st st-n" style="display:none"></div>
  <button class="btn btn-y" id="bRec" onclick="startRec()" disabled>Export Overlay Video</button>
  <button class="btn btn-r" id="bStop" onclick="stopRec()" style="display:none">&#9632; Cancel Export</button>
  <div id="pbar" style="display:none;height:6px;background:var(--elem);border-radius:3px;margin-bottom:8px;overflow:hidden">
    <div id="pfill" style="height:100%;background:var(--a);width:0;transition:width .4s;border-radius:3px"></div>
  </div>
  <div id="recSt" class="st st-n" style="display:none"></div>
  <button class="btn btn-y" id="bSave" onclick="saveVideo()" style="display:none">&#8681; Save Overlay to Phone</button>
  <p class="hint" style="margin-top:8px;margin-bottom:0">In CapCut: add your game video, then add the overlay as a <b style="color:#fff">Picture in Picture</b> layer. Select the overlay clip &rarr; <b style="color:#fff">Effects &rarr; Chroma Key</b>, tap the green area, and it disappears. Position the card wherever you want on the frame. Align its start with the moment the game starts &#8212; scores appear automatically at the right time.</p>
</div>

<script>
var CV=document.getElementById('cv'),CTX=CV.getContext('2d');
var log=[],mr=null,chunks=[],raf=null,wl=null,cardW=0,savedBlob=null,savedMime='',prefMime='';
var SCALE=3; // render canvas at 3× for crisp video export
var _exportMode=false; // true during export: drawOverlay skips canvas resize

// ── Score log ────────────────────────────────────────────────
async function fetchLog(){
  var s=document.getElementById('logSt');
  try{
    var r=await fetch('/score-log');
    log=await r.json();
    if(!log.length){
      s.className='st st-err';
      s.textContent='No events yet — play a game first!';
    } else {
      var dur=(log[log.length-1].t/60000).toFixed(1);
      s.className='st st-ok';
      s.textContent=log.length+' events · '+dur+' min game recorded';
      document.getElementById('bRec').disabled=false;
      updateDurInfo();
      redraw();
      // canvas is sized/scaled inside drawOverlay; second redraw picks up the correct dimensions
      if(cardW>0)redraw();
    }
  }catch(e){
    s.className='st st-err';
    s.textContent='Could not load score log.';
  }
}

function stateAt(ms){
  if(!log.length)return null;
  if(ms<0||ms<log[0].t)return{sA:0,sB:0,setA:0,setB:0,hA:[0,0,0],hB:[0,0,0],wp:log[0].wp,hc:log[0].hc,fmt:log[0].fmt,fs:log[0].fs};
  var s=log[0];
  for(var i=1;i<log.length;i++){if(log[i].t>ms)break;s=log[i];}
  return s;
}

// ── Canvas ───────────────────────────────────────────────────
function rrect(x,y,w,h,r){
  CTX.beginPath();
  CTX.moveTo(x+r,y);CTX.lineTo(x+w-r,y);CTX.quadraticCurveTo(x+w,y,x+w,y+r);
  CTX.lineTo(x+w,y+h-r);CTX.quadraticCurveTo(x+w,y+h,x+w-r,y+h);
  CTX.lineTo(x+r,y+h);CTX.quadraticCurveTo(x,y+h,x,y+h-r);
  CTX.lineTo(x,y+r);CTX.quadraticCurveTo(x,y,x+r,y);
  CTX.closePath();
}
// Rounded rect with per-corner radii: tl, tr, br, bl (0 = square)
function rrectp(x,y,w,h,tl,tr,br,bl){
  CTX.beginPath();
  CTX.moveTo(x+tl,y);
  CTX.lineTo(x+w-tr,y);
  tr?CTX.quadraticCurveTo(x+w,y,x+w,y+tr):CTX.lineTo(x+w,y);
  CTX.lineTo(x+w,y+h-br);
  br?CTX.quadraticCurveTo(x+w,y+h,x+w-br,y+h):CTX.lineTo(x+w,y+h);
  CTX.lineTo(x+bl,y+h);
  bl?CTX.quadraticCurveTo(x,y+h,x,y+h-bl):CTX.lineTo(x,y+h);
  CTX.lineTo(x,y+tl);
  tl?CTX.quadraticCurveTo(x,y,x+tl,y):CTX.lineTo(x,y);
  CTX.closePath();
}

// Mirror of C++ getServeInfo() in score.h — serveTotal:1 = first serve or deuce, 2 = normal rotation
function getServeInfo(s){
  var wp=s.wp||21,sA=s.sA||0,sB=s.sB||0;
  var total=sA+sB,hi=Math.max(sA,sB),lo=Math.min(sA,sB);
  var firstIsA=(s.fs===0);
  if(hi>=wp&&hi-lo<=1){
    var ds=2*wp-1,dp=total-ds,dg=Math.floor((ds-1)/2);
    var fad=(dg%2===0)?!firstIsA:firstIsA;
    return{teamAServing:(dp%2===0)?fad:!fad,servesLeft:1,serveTotal:1};
  }
  if(total===0)return{teamAServing:firstIsA,servesLeft:1,serveTotal:1};
  var group=Math.floor((total-1)/2),pos=(total-1)%2;
  var tAs=(group%2===0)?!firstIsA:firstIsA;
  return{teamAServing:tAs,servesLeft:2-pos,serveTotal:2};
}

// serveInfo: {servesLeft:1|2, serveTotal:1|2} if serving, null if not
function drawRow(name,team,s,ry,boxX,dotCX,serveInfo){
  var RH=47,BS=38,BG=6,BR=7,PAD=16,DOTR=4;
  CTX.fillStyle='#111';
  CTX.font='bold 22px "Helvetica Neue",Arial,sans-serif';
  CTX.textAlign='left';CTX.textBaseline='middle';
  CTX.fillText(name,PAD,ry+RH/2);
  if(serveInfo){
    if(serveInfo.serveTotal===2){
      var dy1=ry+RH/2-DOTR-3,dy2=ry+RH/2+DOTR+3;
      CTX.beginPath();CTX.arc(dotCX,dy1,DOTR,0,2*Math.PI);
      CTX.fillStyle=serveInfo.servesLeft<2?'#9ca3af':'#4a72c0';CTX.fill();
      CTX.beginPath();CTX.arc(dotCX,dy2,DOTR,0,2*Math.PI);
      CTX.fillStyle='#4a72c0';CTX.fill();
    } else {
      CTX.beginPath();CTX.arc(dotCX,ry+RH/2,DOTR,0,2*Math.PI);
      CTX.fillStyle='#4a72c0';CTX.fill();
    }
  }
  var sp=s.setA+s.setB,bx=boxX;
  for(var i=0;i<=sp;i++){
    var cur=(i===sp);
    var my =cur?(team==='A'?s.sA:s.sB):(team==='A'?s.hA[i]:s.hB[i]);
    var opp=cur?(team==='A'?s.sB:s.sA):(team==='A'?s.hB[i]:s.hA[i]);
    var won=!cur&&my>opp,lost=!cur&&my<opp;
    var by=ry+(RH-BS)/2;
    CTX.fillStyle='#fca5a5';
    rrect(bx,by,BS,BS,BR);CTX.fill();
    if(cur){
      CTX.strokeStyle='#dc2626';CTX.lineWidth=2.5;
      rrect(bx+1.25,by+1.25,BS-2.5,BS-2.5,BR-1);CTX.stroke();
    }
    CTX.fillStyle='#111';
    CTX.font=(won||cur?'bold 20px':'400 16px')+' "Helvetica Neue",Arial,sans-serif';
    CTX.textAlign='center';CTX.textBaseline='middle';
    CTX.fillText(String(my),bx+BS/2,ry+RH/2);
    bx+=BS+BG;
  }
}

function drawOverlay(st){
  var H=150,PAD=16,BS=38,BG=6,DOTGAP=24,CR=10;
  var HDR_H=28,ROW_H=47,FTR_H=28;
  var tn=document.getElementById('tournament').value||'Tournament';
  var loc=document.getElementById('location').value;
  var tA=document.getElementById('tA').value||'Team A';
  var tB=document.getElementById('tB').value||'Team B';
  var rnd=document.getElementById('rnd').value;
  var s=st||{sA:0,sB:0,setA:0,setB:0,hA:[0,0,0],hB:[0,0,0],wp:21,hc:0,fmt:2,fs:0};
  var hdrTxt=loc?tn+' – '+loc:tn;
  var fl=s.fmt===0?'1 Set':s.fmt===1?'2 Sets':'Best of 3';
  var ftTxt=rnd+'  |  '+fl+' to '+s.wp+(s.hc>0?' – HC '+s.hc:'');
  // Measure at 1x (before scale transform)
  CTX.font='bold 22px "Helvetica Neue",Arial,sans-serif';
  var nW=Math.max(CTX.measureText(tA).width,CTX.measureText(tB).width);
  CTX.font='bold 16px "Helvetica Neue",Arial,sans-serif';
  var hdrW=CTX.measureText(hdrTxt).width;
  CTX.font='600 12px "Helvetica Neue",Arial,sans-serif';
  var ftW=CTX.measureText(ftTxt).width;
  var sp=s.setA+s.setB,nb=sp+1;
  var boxesW=nb*BS+(nb-1)*BG;
  // boxX pushed right so score columns clear the salmon footer tag
  var ftTagW_nat=ftW+2*PAD;
  var dotCX=PAD+nW+DOTGAP/2;
  var boxX=Math.max(PAD+nW+DOTGAP,ftTagW_nat+8);
  // Right margin matches the vertical gap between squares and row edge: (ROW_H-BS)/2
  var bodyW=boxX+boxesW+(ROW_H-BS)/2;
  var hdrTagW=Math.min(hdrW+2*PAD,bodyW);
  var ftTagW=Math.min(ftTagW_nat,bodyW);
  cardW=bodyW;
  // Canvas at SCALE× for crisp export; guard avoids clearing on every animation frame.
  // During export (_exportMode) the canvas is pre-sized to the max state and must not resize —
  // a mid-stream dimension change breaks both VP9 captureStream and the H.264 WebCodecs encoder.
  var cw=Math.ceil(cardW*SCALE),ch=H*SCALE;
  if(cw%2)cw++;
  if(!_exportMode){
    if(CV.width!==cw||CV.height!==ch){CV.width=cw;CV.height=ch;}
    CV.style.width=Math.ceil(cardW)+'px';CV.style.height=H+'px';
  }
  // Serve info mirrors C++ getServeInfo
  var srv=getServeInfo(s);
  var infoA=srv.teamAServing?{servesLeft:srv.servesLeft,serveTotal:srv.serveTotal}:null;
  var infoB=srv.teamAServing?null:{servesLeft:srv.servesLeft,serveTotal:srv.serveTotal};
  CTX.clearRect(0,0,CV.width,CV.height); // always clear full locked buffer
  CTX.save();
  CTX.scale(SCALE,SCALE);
  // Green fill covers the full canvas (includes extra space to the right when locked at max width)
  CTX.fillStyle='#00FF00';CTX.fillRect(0,0,CV.width/SCALE,CV.height/SCALE);
  // White team rows area — right corners rounded so the center has a clean right edge
  CTX.fillStyle='#fff';rrectp(0,HDR_H,cardW,2*ROW_H,0,CR,CR,0);CTX.fill();
  // Blue header badge — top corners rounded (matches reference CSS: border-radius 4px 4px 0 0)
  CTX.fillStyle='#4a72c0';
  rrectp(0,0,hdrTagW,HDR_H,CR,CR,0,0);CTX.fill();
  CTX.fillStyle='#fff';
  CTX.font='bold 16px "Helvetica Neue",Arial,sans-serif';
  CTX.textAlign='left';CTX.textBaseline='middle';
  CTX.fillText(hdrTxt,PAD,HDR_H/2);
  // Team rows
  drawRow(tA,'A',s,HDR_H,boxX,dotCX,infoA);
  drawRow(tB,'B',s,HDR_H+ROW_H,boxX,dotCX,infoB);
  // Salmon footer badge — bottom corners rounded (matches reference CSS: border-radius 0 0 4px 4px)
  var ftrY=HDR_H+2*ROW_H;
  CTX.fillStyle='#fca5a5';
  rrectp(0,ftrY,ftTagW,FTR_H,0,0,CR,CR);CTX.fill();
  CTX.fillStyle='#7f1d1d';
  CTX.font='600 12px "Helvetica Neue",Arial,sans-serif';
  CTX.textAlign='left';CTX.textBaseline='middle';
  CTX.fillText(ftTxt,PAD,ftrY+FTR_H/2);
  CTX.restore();
}

function redraw(){drawOverlay(log.length?log[log.length-1]:null);}

function exportPNG(){
  var tmp=document.createElement('canvas');
  tmp.width=CV.width;tmp.height=CV.height;
  tmp.getContext('2d').drawImage(CV,0,0);
  tmp.toBlob(function(b){
    var a=document.createElement('a');
    a.href=URL.createObjectURL(b);
    a.download='overlay.png';
    a.click();
  },'image/png');
}

// ── Export ───────────────────────────────────────────────────
function getSyncMs(){
  var mm=parseInt(document.getElementById('syncMm').value)||0;
  var ss=parseInt(document.getElementById('syncSs').value)||0;
  return(mm*60+ss)*1000;
}

function calcSpeed(){
  if(!log.length)return{totalMs:0,speedFactor:1,recMs:0};
  var prePadMs=getSyncMs();
  var totalMs=prePadMs+(log[log.length-1].t-log[0].t)+5000;
  // Cap at 16: 16×1,000,000=16,000,000 fits in Chrome's 3-byte TimecodeScale VINT (max 16,777,215).
  // Beyond cap the recording takes totalMs/16 seconds (e.g. 75s for a 20-min game) — still fast.
  var speedFactor=Math.min(16,Math.max(1,Math.ceil(totalMs/20000)));
  return{totalMs:totalMs,speedFactor:speedFactor,recMs:Math.ceil(totalMs/speedFactor)};
}

function updateDurInfo(){
  if(!log.length)return;
  var sp=calcSpeed();
  var gm=Math.floor(sp.totalMs/60000)|0,gs=Math.round((sp.totalMs%60000)/1000);
  var rs=Math.ceil(sp.recMs/1000);
  var d=document.getElementById('durInfo');
  d.style.display='block';d.className='st st-n';
  d.textContent='Overlay: '+gm+'m '+gs+'s. Export takes ~'+rs+'s.';
}

// Patch MP4 container: multiply all sample durations and header durations by factor.
// Handles fragmented MP4 (fMP4) produced by Chrome 130+ MediaRecorder.
function patchMp4Speed(buf,factor){
  if(factor===1)return;
  var b=new Uint8Array(buf),v=new DataView(buf);
  function r32(o){return v.getUint32(o);}
  function w32(o,n){if(n>0&&n>>>0<0x80000000)v.setUint32(o,n>>>0);}
  function walk(s,e){
    var p=s;
    while(p+8<=e){
      var sz=r32(p); if(sz<8||sz===1||p+sz>e)break;
      var ty=String.fromCharCode(b[p+4],b[p+5],b[p+6],b[p+7]);
      var ver=b[p+8],fl=r32(p+8)&0xFFFFFF;
      if(ty==='mvhd'||ty==='mdhd'){
        // v0: duration at +24; v1: low 4B of uint64 at +36
        var d=(ver===1)?p+36:p+24; w32(d,r32(d)*factor);
      } else if(ty==='tkhd'){
        // v0: duration at +28; v1: low 4B at +44
        var d=(ver===1)?p+44:p+28; w32(d,r32(d)*factor);
      } else if(ty==='tfhd'){
        var q=p+16; // after size+type+ver+flags+trackID
        if(fl&0x1)q+=8; if(fl&0x2)q+=4;
        if(fl&0x8){w32(q,r32(q)*factor);} // default_sample_duration
      } else if(ty==='trun'){
        var sc=r32(p+12),q=p+16;
        if(fl&0x1)q+=4; if(fl&0x4)q+=4; // data_offset, first_sample_flags
        for(var i=0;i<sc;i++){
          if(fl&0x100){w32(q,r32(q)*factor);q+=4;} // sample_duration
          if(fl&0x200)q+=4; if(fl&0x400)q+=4; if(fl&0x800)q+=4;
        }
      }
      if('moov trak mdia minf stbl moof traf'.indexOf(ty)>=0)walk(p+8,p+sz);
      p+=sz;
    }
  }
  walk(0,buf.byteLength);
}

// Patch WebM Segment Info: multiply TimecodeScale by speedFactor and ensure Duration is set.
// Chrome MediaRecorder leaves Duration=0 (or omits it) making the file non-seekable; the gallery
// "goes back to current time" when you try to drag — and CapCut treats a 0-duration clip as empty.
// Returns the (possibly expanded) Uint8Array to use for the Blob — caller must use the return value.
function patchTimecodeScale(bytes,factor,recMs){
  if(factor===1)return bytes;
  var lim=Math.min(bytes.length,16384),tcEnd=-1,infoSzOff=-1,infoSzLen=0,durFound=false;
  for(var i=0;i<lim-6;i++){
    // Segment Info (0x1549A966) — remember size-VINT position so we can mark it unknown if we insert
    if(bytes[i]===0x15&&bytes[i+1]===0x49&&bytes[i+2]===0xA9&&bytes[i+3]===0x66&&infoSzOff<0){
      infoSzOff=i+4;
      var b0=bytes[infoSzOff];
      infoSzLen=b0>=0x80?1:b0>=0x40?2:b0>=0x20?3:b0>=0x10?4:b0>=0x08?5:b0>=0x04?6:b0>=0x02?7:8;
    }
    // TimecodeScale (0x2AD7B1)
    if(bytes[i]===0x2A&&bytes[i+1]===0xD7&&bytes[i+2]===0xB1){
      var sv=bytes[i+3],size,off;
      if(sv&0x80){size=sv&0x7F;off=i+4;}
      else if(sv&0x40){size=((sv&0x3F)<<8)|bytes[i+4];off=i+5;}
      else continue;
      if(size<1||size>8||off+size>bytes.length)continue;
      var v=0;for(var j=0;j<size;j++)v=v*256+bytes[off+j];
      var nv=Math.round(v*factor);
      // Expand the VINT value field if nv overflows the existing size bytes.
      // Chrome stores TimecodeScale=1,000,000 in 3 bytes (max 16,777,215); speedFactor>16
      // overflows it — e.g. a 20-min game gives speedFactor=61, nv=61,000,000 which needs 4 bytes.
      // Overflow silently truncates the MSB, writing the wrong scale (~10.6× instead of 61×).
      if(nv>Math.pow(256,size)-1&&size<8&&(sv&0x80)){
        var newSize=size;while(newSize<8&&Math.pow(256,newSize)-1<nv)newSize++;
        var extra=newSize-size;
        bytes[i+3]=0x80|newSize;
        var nb2=new Uint8Array(bytes.length+extra);
        nb2.set(bytes.subarray(0,off));nb2.set(bytes.subarray(off),off+extra);
        bytes=nb2;
        lim=Math.min(bytes.length,lim+extra);
        size=newSize;
        // SegmentInfo's declared size is now `extra` bytes short; mark it unknown-size so
        // parsers scan for child element IDs rather than stopping at the old end boundary.
        if(infoSzOff>=0){
          var unkB=[[],[0xFF],[0x7F,0xFF],[0x3F,0xFF,0xFF],[0x1F,0xFF,0xFF,0xFF],
                   [0x0F,0xFF,0xFF,0xFF,0xFF],[0x07,0xFF,0xFF,0xFF,0xFF,0xFF],
                   [0x03,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF],[0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF]];
          var uk=unkB[infoSzLen]||[];for(var k=0;k<uk.length;k++)bytes[infoSzOff+k]=uk[k];
        }
      }
      var tmp=nv;for(var j=size-1;j>=0;j--){bytes[off+j]=tmp&0xFF;tmp=Math.floor(tmp/256);}
      tcEnd=off+size;
    }
    // Duration (0x4489) — Chrome may write float64 (size=0x88) or float32 (size=0x84) or omit it
    if(bytes[i]===0x44&&bytes[i+1]===0x89&&!durFound){
      if(bytes[i+2]===0x88&&i+11<=bytes.length){
        var dv=new DataView(bytes.buffer,bytes.byteOffset+i+3,8);
        if(!(dv.getFloat64(0,false)>0))dv.setFloat64(0,recMs,false);
        durFound=true;
      } else if(bytes[i+2]===0x84&&i+7<=bytes.length){
        var dv=new DataView(bytes.buffer,bytes.byteOffset+i+3,4);
        if(!(dv.getFloat32(0,false)>0))dv.setFloat32(0,recMs,false);
        durFound=true;
      }
    }
  }
  if(durFound||tcEnd<0)return bytes;
  // Duration absent: mark Segment Info as unknown-size then splice a Duration element after TimecodeScale.
  // Marking Info as unknown-size means parsers read until the next Level-1 element instead of stopping
  // at the old declared size, so they find the newly inserted Duration element.
  if(infoSzOff>=0){
    var unk=[[],[0xFF],[0x7F,0xFF],[0x3F,0xFF,0xFF],[0x1F,0xFF,0xFF,0xFF],
             [0x0F,0xFF,0xFF,0xFF,0xFF],[0x07,0xFF,0xFF,0xFF,0xFF,0xFF],
             [0x03,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF],[0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF]];
    var u=unk[infoSzLen]||[];
    for(var k=0;k<u.length;k++)bytes[infoSzOff+k]=u[k];
  }
  var ins=new Uint8Array(11);
  ins[0]=0x44;ins[1]=0x89;ins[2]=0x88;
  new DataView(ins.buffer,3,8).setFloat64(0,recMs,false);
  var nb=new Uint8Array(bytes.length+11);
  nb.set(bytes.subarray(0,tcEnd));
  nb.set(ins,tcEnd);
  nb.set(bytes.subarray(tcEnd),tcEnd+11);
  return nb;
}

// ── Minimal MP4 muxer (H.264, no audio) ────────────────────────────────────
// Produces a valid non-fragmented MP4 with moov after mdat (fine for local import).
function muxMp4(samp,descBuf,W,H){
  var n=samp.length,tscale=1000;
  var durMs=Math.round((samp[n-1].ts+samp[n-1].dur)/1000);
  var szArr=samp.map(function(s){return s.buf.byteLength;});
  var total=szArr.reduce(function(a,b){return a+b;},0);
  // Use chunk.duration if set; otherwise derive from consecutive timestamps (chunk.duration may be null)
  var dArr=samp.map(function(s,i){var d=s.dur>0?s.dur:i<n-1?samp[i+1].ts-s.ts:5000000;return Math.max(1,Math.round(d/1000));});
  function B(a){return new Uint8Array(a);}
  function u16(v){var b=B(2);new DataView(b.buffer).setUint16(0,v>>>0);return b;}
  function u32(v){var b=B(4);new DataView(b.buffer).setUint32(0,v>>>0);return b;}
  function s4(s){return B([s.charCodeAt(0),s.charCodeAt(1),s.charCodeAt(2),s.charCodeAt(3)]);}
  function cat(){var t=0,i;for(i=0;i<arguments.length;i++)t+=arguments[i].length;var r=B(t),o=0;for(i=0;i<arguments.length;i++){r.set(arguments[i],o);o+=arguments[i].length;}return r;}
  function bx(t){var a=[].slice.call(arguments,1);var p=cat.apply(null,a);return cat(u32(8+p.length),s4(t),p);}
  function fb(t,v,f){var a=[].slice.call(arguments,3);return bx.apply(null,[t,B([v,f>>16&255,f>>8&255,f&255])].concat(a));}
  var avcCD=new Uint8Array(descBuf);
  var avcCBx=cat(u32(8+avcCD.length),s4('avcC'),avcCD);
  var avc1=bx('avc1',B(6),u16(1),B(16),u16(W),u16(H),u32(0x00480000),u32(0x00480000),B(4),u16(1),B(32),u16(0x18),B([0xFF,0xFF]),avcCBx);
  var stsd=fb('stsd',0,0,u32(1),avc1);
  var se=[],i=0;while(i<n){var c=1,d=dArr[i];while(i+c<n&&dArr[i+c]===d)c++;se.push(c,d);i+=c;}
  var sd=B(se.length*4),sv=new DataView(sd.buffer);se.forEach(function(v,j){sv.setUint32(j*4,v);});
  var stts=fb('stts',0,0,u32(se.length/2),sd);
  var ki=samp.map(function(s,i){return s.key?i+1:0;}).filter(Boolean);
  var kd=B(ki.length*4),kv=new DataView(kd.buffer);ki.forEach(function(x,j){kv.setUint32(j*4,x);});
  var stss=fb('stss',0,0,u32(ki.length),kd);
  var szD=B(n*4),szV=new DataView(szD.buffer);szArr.forEach(function(s,i){szV.setUint32(i*4,s);});
  var stsz=fb('stsz',0,0,u32(0),u32(n),szD);
  var stsc=fb('stsc',0,0,u32(1),u32(1),u32(1),u32(1));
  var ftLen=bx('ftyp',s4('isom'),u32(0x200),s4('isom'),s4('avc1')).length;
  var stcoD=B(n*4),stcoV=new DataView(stcoD.buffer);
  var off=ftLen+8;for(i=0;i<n;i++){stcoV.setUint32(i*4,off);off+=szArr[i];}
  var stco=fb('stco',0,0,u32(n),stcoD);
  var stbl=bx('stbl',stsd,stts,stss,stsz,stsc,stco);
  var dref=fb('dref',0,0,u32(1),fb('url ',0,1));
  var minf=bx('minf',fb('vmhd',0,1,u16(0),B(6)),bx('dinf',dref),stbl);
  var hn=B([86,105,100,101,111,72,97,110,100,108,101,114,0]);
  var hdlr=fb('hdlr',0,0,u32(0),s4('vide'),B(12),hn);
  var mdhd=fb('mdhd',0,0,u32(0),u32(0),u32(tscale),u32(durMs),u16(0x55C4),u16(0));
  var mdia=bx('mdia',mdhd,hdlr,minf);
  var mt=cat(u32(0x10000),u32(0),u32(0),u32(0),u32(0x10000),u32(0),u32(0),u32(0),u32(0x40000000));
  var tkhd=fb('tkhd',0,3,u32(0),u32(0),u32(1),u32(0),u32(durMs),B(8),u16(0),u16(0),u16(0),u16(0),mt,u32(W<<16),u32(H<<16));
  var mvhd=fb('mvhd',0,0,u32(0),u32(0),u32(tscale),u32(durMs),u32(0x10000),u16(0x100),B(10),mt,B(24),u32(2));
  var moov=bx('moov',mvhd,bx('trak',tkhd,mdia));
  var mdParts=[u32(8+total),s4('mdat')];samp.forEach(function(s){mdParts.push(new Uint8Array(s.buf));});
  var ftyp=bx('ftyp',s4('isom'),u32(0x200),s4('isom'),s4('avc1'));
  return cat(ftyp,cat.apply(null,mdParts),moov).buffer;
}

// ── WebCodecs export: encode H.264 → proper MP4, no speed-up needed ─────────
async function startExportWebCodecs(){
  var bRec=document.getElementById('bRec'),recSt=document.getElementById('recSt');
  bRec.disabled=true;bRec.textContent='Exporting…';
  recSt.style.display='block';recSt.className='st st-n';recSt.textContent='Encoding…';
  document.getElementById('bSave').style.display='none';
  var prePadMs=getSyncMs(),firstT=log[0].t;
  var totalMs=prePadMs+(log[log.length-1].t-firstT)+5000;
  var initSt={sA:0,sB:0,setA:0,setB:0,hA:[0,0,0],hB:[0,0,0],wp:log[0].wp,hc:log[0].hc,fmt:log[0].fmt};
  // Pre-size canvas to the final (widest) state, then lock so mid-export set completions
  // don't change CV.width — which would break the H.264 encoder (frame size mismatch).
  drawOverlay(log[log.length-1]);
  _exportMode=true;
  drawOverlay(initSt); // re-draw initial state on the now-locked canvas
  var W=CV.width,H=CV.height; // max dimensions, SCALE× from drawOverlay
  var samples=[],configDesc=null,encErr=false;
  var enc=new VideoEncoder({
    output:function(chunk,meta){
      if(meta&&meta.decoderConfig&&meta.decoderConfig.description&&!configDesc)
        configDesc=meta.decoderConfig.description;
      var buf=new ArrayBuffer(chunk.byteLength);chunk.copyTo(buf);
      samples.push({buf:buf,ts:chunk.timestamp,dur:chunk.duration||0,key:chunk.type==='key'});
    },
    error:function(e){console.error('VideoEncoder:',e);encErr=true;}
  });
  // Baseline L3.1 (42E01F) is universally supported by Android hardware encoders and covers
  // up to 3600 MBs (≈720p). Our max canvas (3-set: 1056×450 = 1914 MBs) fits comfortably.
  // Main L4.0 (4D0028) was rejected by many Android hardware encoders via VideoEncoder.
  try{enc.configure({codec:'avc1.42E01F',width:W,height:H,bitrate:200000,avc:{format:'avc'}});}
  catch(e){enc.close();_exportMode=false;bRec.disabled=false;bRec.textContent='Export Overlay Video';
    recSt.textContent='H.264 not available — falling back to WebM…';setTimeout(startRec,100);return;}
  function encFrame(st,tMs,dMs){
    drawOverlay(st);
    var f=new VideoFrame(CV,{timestamp:tMs*1000,duration:dMs*1000});
    enc.encode(f,{keyFrame:true});f.close();
  }
  if(prePadMs>0)encFrame(initSt,0,prePadMs);
  for(var i=0;i<log.length;i++){
    if(encErr)break;
    var tMs=prePadMs+(log[i].t-firstT);
    var nMs=(i<log.length-1)?prePadMs+(log[i+1].t-firstT):totalMs;
    if(nMs>tMs)encFrame(log[i],tMs,nMs-tMs);
  }
  try{await enc.flush();}catch(e){encErr=true;}
  enc.close();
  _exportMode=false;
  if(encErr||!samples.length||!configDesc){
    bRec.disabled=false;bRec.textContent='Export Overlay Video';
    recSt.textContent='H.264 encoding failed — falling back to WebM…';setTimeout(startRec,100);return;
  }
  var mp4=muxMp4(samples,configDesc,W,H);
  savedBlob=new Blob([mp4],{type:'video/mp4'});savedMime='video/mp4';
  recSt.className='st st-ok';
  recSt.textContent='Export done ('+Math.round(mp4.byteLength/1024)+'KB). Tap below to save.';
  bRec.disabled=false;bRec.textContent='Export Overlay Video';
  document.getElementById('bSave').style.display='block';
}

function exportOverlay(){
  if(!log.length){alert('No score log — play a game first.');return;}
  if(typeof VideoEncoder!=='undefined')startExportWebCodecs();
  else startRec();
}

// Draw each log event on the canvas, hold it for its (compressed) duration, let MediaRecorder
// capture the stream.  One event = one held frame; no stateAt/elapsed/gamePos math needed.
async function startRec(){
  if(!log.length){alert('No score log — play a game first.');return;}
  if(!CV.captureStream){alert('Canvas capture not supported. Try Chrome.');return;}
  chunks=[];
  var prePadMs=getSyncMs(),sp=calcSpeed();
  var totalMs=sp.totalMs,speedFactor=sp.speedFactor,recMs=sp.recMs;
  var initSt={sA:0,sB:0,setA:0,setB:0,hA:[0,0,0],hB:[0,0,0],wp:log[0].wp,hc:log[0].hc,fmt:log[0].fmt,fs:log[0].fs};
  drawOverlay(log[log.length-1]);_exportMode=true;drawOverlay(initSt);
  var mime=MediaRecorder.isTypeSupported('video/webm;codecs=vp9')?'video/webm;codecs=vp9':'video/webm';
  var stream=CV.captureStream(30);
  mr=new MediaRecorder(stream,{mimeType:mime,videoBitsPerSecond:500000});
  mr.ondataavailable=function(e){if(e.data.size>0)chunks.push(e.data);};
  mr.onstop=function(){
    _exportMode=false;
    if(wl){wl.release();wl=null;}
    if(!chunks.length){
      var st=document.getElementById('recSt');
      st.className='st st-err';st.textContent='Recording failed — try refreshing.';
      document.getElementById('bStop').style.display='none';
      document.getElementById('pbar').style.display='none';
      document.getElementById('bRec').disabled=false;document.getElementById('bRec').textContent='Export Overlay Video';
      return;
    }
    var raw=new Blob(chunks,{type:mime});
    raw.arrayBuffer().then(function(buf){
      var out=patchTimecodeScale(new Uint8Array(buf),speedFactor,recMs);
      savedBlob=new Blob([out],{type:mime});savedMime=mime;
      var st=document.getElementById('recSt');
      st.className='st st-ok';st.textContent='Done ('+Math.round(out.byteLength/1024)+'KB) — tap Save.';
      document.getElementById('bStop').style.display='none';document.getElementById('pbar').style.display='none';
      document.getElementById('bRec').disabled=false;document.getElementById('bRec').textContent='Export Overlay Video';
      document.getElementById('bSave').style.display='block';
    });
  };
  mr.start();
  var bRec=document.getElementById('bRec'),recSt=document.getElementById('recSt');
  bRec.disabled=true;bRec.textContent='Recording…';
  document.getElementById('bStop').style.display='block';
  document.getElementById('pbar').style.display='block';
  document.getElementById('pfill').style.width='0';
  recSt.style.display='block';recSt.className='st st-n';recSt.textContent='Starting…';
  if(navigator.wakeLock)navigator.wakeLock.request('screen').then(function(l){wl=l;}).catch(function(){});
  // hold(ms): wait ms real-time milliseconds while MediaRecorder captures the current canvas frame
  function hold(ms){return new Promise(function(r){setTimeout(r,Math.max(16,Math.round(ms)));});}
  // Pre-pad: show 0-0 state for the sync-offset duration (compressed)
  if(prePadMs>0){drawOverlay(initSt);await hold(prePadMs/speedFactor);}
  // Main loop: draw each event then hold for its compressed real-game duration
  for(var i=0;i<log.length;i++){
    if(!mr||mr.state==='inactive')break;
    drawOverlay(log[i]);
    var dMs=(i<log.length-1)?(log[i+1].t-log[i].t):5000;
    document.getElementById('pfill').style.width=Math.round((i+1)*100/log.length)+'%';
    recSt.textContent='Recording event '+(i+1)+' / '+log.length+'…';
    await hold(dMs/speedFactor);
  }
  if(mr&&mr.state!=='inactive')mr.stop();
}

function stopRec(){
  if(mr&&mr.state!=='inactive')mr.stop();
}

function saveVideo(){
  if(!savedBlob)return;
  var ext=savedMime.includes('mp4')?'mp4':'webm';
  var filename='overlay.'+ext;
  // Try Web Share API first (works when served over HTTPS)
  if(navigator.share&&navigator.canShare){
    try{
      var f=new File([savedBlob],filename,{type:savedMime});
      if(navigator.canShare({files:[f]})){
        navigator.share({files:[f],title:'Scoreboard Overlay'}).catch(function(e){
          if(e.name!=='AbortError') uploadAndDownload(filename);
        });
        return;
      }
    }catch(e){}
  }
  uploadAndDownload(filename);
}

function uploadAndDownload(filename){
  var bSave=document.getElementById('bSave');
  var st=document.getElementById('recSt');
  bSave.disabled=true; bSave.textContent='Uploading...';
  st.style.display='block'; st.className='st st-n';
  st.textContent='Uploading to scoreboard... please wait.';
  var fd=new FormData();
  fd.append('video',savedBlob,filename);
  fetch('/overlay-cache',{method:'POST',body:fd})
    .then(function(r){
      if(!r.ok) throw new Error(r.status===507?'Not enough memory (game too long?)':'Upload failed ('+r.status+')');
      var a=document.createElement('a');
      a.href='/overlay-download'; a.download=filename;
      document.body.appendChild(a); a.click(); document.body.removeChild(a);
      st.className='st st-ok'; st.textContent='Download started! Check your notifications.';
      bSave.disabled=false; bSave.textContent='Download again';
    })
    .catch(function(e){
      st.textContent='Error: '+e.message+' — tap below to open in new tab instead.';
      bSave.disabled=false; bSave.textContent='Open video in new tab';
      bSave.onclick=function(){
        var url=URL.createObjectURL(savedBlob);
        var a=document.createElement('a'); a.href=url; a.target='_blank';
        document.body.appendChild(a); a.click();
        setTimeout(function(){document.body.removeChild(a);},500);
      };
    });
}

fetchLog();redraw();
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

  // Video overlay export page
  server->on("/overlay", HTTP_GET, []() {
    server->send_P(200, "text/html", OVERLAY_HTML);
  });

  // Score event log (JSON array of timestamped states)
  server->on("/score-log", HTTP_GET, []() {
    server->send(200, "application/json", ScoreLogger::toJson());
  });

  // Overlay video cache — browser uploads blob here, then downloads via HTTP (bypasses blob: URL limit on Android)
  server->on("/overlay-cache", HTTP_POST,
    []() { server->send(_ovCacheSize > 0 ? 200 : 507, "text/plain", _ovCacheSize > 0 ? "ok" : "oom"); },
    _handleOverlayUpload
  );
  server->on("/overlay-download", HTTP_GET, []() {
    if (!_ovCache || _ovCacheSize == 0) { server->send(404, "text/plain", "no video"); return; }
    server->sendHeader("Content-Disposition", "attachment; filename=\"overlay.webm\"");
    server->send_P(200, _ovMime, (PGM_P)_ovCache, _ovCacheSize);
  });

  // Status JSON (score + mode + WiFi + brightness)
  server->on("/status", HTTP_GET, []() {
    xSemaphoreTake(scoreMutex, portMAX_DELAY);
    uint8_t sA = currentScore.scoreA, sB = currentScore.scoreB;
    uint8_t sSetA = currentScore.setA, sSetB = currentScore.setB;
    uint8_t sFirst = currentScore.firstServer;
    uint8_t sHistA[3], sHistB[3];
    memcpy(sHistA, currentScore.histA, 3);
    memcpy(sHistB, currentScore.histB, 3);
    ServeInfo srv = getServeInfo(currentScore);
    xSemaphoreGive(scoreMutex);
    String json = "{";
    json += "\"scoreA\":" + String(sA) + ",";
    json += "\"scoreB\":" + String(sB) + ",";
    json += "\"setA\":" + String(sSetA) + ",";
    json += "\"setB\":" + String(sSetB) + ",";
    json += "\"firstServer\":" + String(sFirst) + ",";
    json += "\"serving\":" + String(srv.teamAServing ? 0 : 1) + ",";
    json += "\"servesLeft\":" + String(srv.servesLeft) + ",";
    json += "\"serveTotal\":" + String(srv.serveTotal) + ",";
    json += "\"winPoints\":" + String(currentScore.winPoints) + ",";
    json += "\"brightness\":" + String(LED::getBrightness()) + ",";
    json += "\"online\":" + String(WiFiMgr::isOnline() ? "true" : "false") + ",";
    json += "\"wifiEnabled\":" + String(WiFiMgr::isStaEnabled() ? "true" : "false") + ",";
    json += "\"ssid\":\"" + WiFiMgr::getSSID() + "\",";
    json += "\"rssi\":" + String(WiFiMgr::getRSSI()) + ",";
    json += "\"boardId\":\"" + WiFiMgr::getScoreboardId() + "\",";
    json += "\"breakTimer\":" + String(ScoreActions::breakTimerRemainingMs() / 1000) + ",";
    json += "\"timeoutTimer\":" + String(ScoreActions::timeoutCountdownMs() / 1000) + ",";
    json += "\"rotations\":" + String(ScoreActions::getRotationCount()) + ",";
    json += "\"setsPlayed\":" + String(sSetA + sSetB) + ",";
    json += "\"histA\":[";
    for (int i = 0; i < sSetA + sSetB && i < 3; i++) { if (i) json += ","; json += String(sHistA[i]); }
    json += "],\"histB\":[";
    for (int i = 0; i < sSetA + sSetB && i < 3; i++) { if (i) json += ","; json += String(sHistB[i]); }
    json += "],\"dimSleep\":" + String(ScoreActions::getDimTimeoutSec());
    json += ",\"sleeping\":" + String(ScoreActions::isDimActive() ? "true" : "false");
    json += ",\"serverIp\":\"" + WsClient::getServerIp() + "\"";
    json += ",\"wsConnected\":" + String(WsClient::isConnected() ? "true" : "false");
    String _modeStr;
    switch (Mode::get()) {
      case AppMode::CENTRAL:  _modeStr = "central";  break;
      case AppMode::FIREBASE: _modeStr = "firebase"; break;
      default:                _modeStr = "local";    break;
    }
    json += ",\"mode\":\"" + _modeStr + "\"";
    json += ",\"fbChannel\":\"" + Firebase::getChannel() + "\"";
    json += ",\"fbPoll\":" + String(Firebase::getPollIntervalSec());
    json += ",\"hardcap\":" + String(currentScore.hardcap);
    json += ",\"format\":" + String(currentScore.format);
    json += ",\"version\":\"" + String(FIRMWARE_VERSION) + "\"";
    json += "}";
    server->send(200, "application/json", json);
  });
  
  // Actions score (modes LOCAL et WRITE)
  server->on("/a/inc",   HTTP_POST, []() { ScoreActions::apply("a/inc");   server->send(200, "text/plain", "OK"); });
  server->on("/a/dec",   HTTP_POST, []() { ScoreActions::apply("a/dec");   server->send(200, "text/plain", "OK"); });
  server->on("/b/inc",   HTTP_POST, []() { ScoreActions::apply("b/inc");   server->send(200, "text/plain", "OK"); });
  server->on("/b/dec",   HTTP_POST, []() { ScoreActions::apply("b/dec");   server->send(200, "text/plain", "OK"); });
  server->on("/nextset", HTTP_POST, []() {
    if (!ScoreActions::apply("nextset")) { server->send(409, "text/plain", "TIED"); return; }
    server->send(200, "text/plain", "OK");
  });
  server->on("/timeout", HTTP_POST, []() { ScoreActions::apply("timeout"); server->send(200, "text/plain", "OK"); });
  server->on("/reset",   HTTP_POST, []() { ScoreActions::apply("reset");   server->send(200, "text/plain", "OK"); });

  server->on("/serve/first", HTTP_POST, []() {
    ScoreActions::notifyActivity();
    String body = server->arg("plain");
    xSemaphoreTake(scoreMutex, portMAX_DELAY);
    currentScore.firstServer = (body == "1") ? 1 : 0;
    LED::update(currentScore);
    xSemaphoreGive(scoreMutex);
    server->send(200, "text/plain", "OK");
  });

  server->on("/hardcap", HTTP_POST, []() {
    ScoreActions::notifyActivity();
    if (server->hasArg("plain")) {
      int hc = server->arg("plain").toInt();
      if (hc >= 0 && hc <= 99) {
        xSemaphoreTake(scoreMutex, portMAX_DELAY);
        currentScore.hardcap = (uint8_t)hc;
        LED::update(currentScore);
        xSemaphoreGive(scoreMutex);
        WsClient::requestPush();
      }
    }
    server->send(200, "text/plain", "OK");
  });

  server->on("/format", HTTP_POST, []() {
    ScoreActions::notifyActivity();
    if (server->hasArg("plain")) {
      int fmt = server->arg("plain").toInt();
      if (fmt >= 0 && fmt <= 2) {
        xSemaphoreTake(scoreMutex, portMAX_DELAY);
        currentScore.format = (uint8_t)fmt;
        xSemaphoreGive(scoreMutex);
        WsClient::requestPush();
      }
    }
    server->send(200, "text/plain", "OK");
  });

  server->on("/winpoints", HTTP_POST, []() {
    ScoreActions::notifyActivity();
    if (server->hasArg("plain")) {
      int wp = server->arg("plain").toInt();
      if (wp >= 5 && wp <= 99) {
        xSemaphoreTake(scoreMutex, portMAX_DELAY);
        currentScore.winPoints = wp;
        LED::update(currentScore);
        xSemaphoreGive(scoreMutex);
      }
    }
    server->send(200, "text/plain", "OK");
  });
  
  // Scoreboard ID
  server->on("/setboardid", HTTP_POST, []() {
    if (server->hasArg("plain")) {
      String id = server->arg("plain");
      id.trim();
      if (id.length() > 0 && id.length() <= 31)
        WiFiMgr::setScoreboardId(id);
    }
    server->send(200, "text/plain", "OK");
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

  server->on("/wifi/enable", HTTP_POST, []() {
    WiFiMgr::enableSta();
    server->send(200, "text/plain", "OK");
  });

  server->on("/wifi/disable", HTTP_POST, []() {
    WiFiMgr::disableSta();
    server->send(200, "text/plain", "OK");
  });

  // Brightness
  server->on("/brightness", HTTP_POST, []() {
    ScoreActions::notifyActivity();
    if (server->hasArg("plain")) {
      uint8_t brightness = constrain(server->arg("plain").toInt(), 1, 255);
      LED::setBrightness(brightness);  // Sauvegarde automatiquement en NVS
      server->send(200, "text/plain", "OK");
    } else {
      server->send(400, "text/plain", "Bad Request");
    }
  });
  
  // Battery saver timeout
  server->on("/dimsleep", HTTP_POST, []() {
    ScoreActions::notifyActivity();
    if (server->hasArg("plain")) {
      uint16_t secs = (uint16_t)constrain(server->arg("plain").toInt(), 0, 3600);
      ScoreActions::setDimTimeout(secs);
      server->send(200, "text/plain", "OK");
    } else {
      server->send(400, "text/plain", "Bad Request");
    }
  });

  server->on("/sleepnow", HTTP_POST, []() {
    ScoreActions::activateSleep();
    server->send(200, "text/plain", "OK");
  });

  server->on("/wake", HTTP_POST, []() {
    ScoreActions::notifyActivity();
    server->send(200, "text/plain", "OK");
  });

  server->on("/serverip", HTTP_POST, []() {
    String ip = server->hasArg("plain") ? server->arg("plain") : "";
    ip.trim();
    WsClient::saveServerIp(ip);
    WsClient::init(ip);
    server->send(200, "text/plain", "OK");
  });

  server->on("/mode", HTTP_POST, []() {
    if (!server->hasArg("plain")) { server->send(400, "text/plain", "Bad"); return; }
    String m = server->arg("plain");

    AppMode newMode;
    if      (m == "local")    newMode = AppMode::LOCAL;
    else if (m == "central")  newMode = AppMode::CENTRAL;
    else if (m == "firebase") newMode = AppMode::FIREBASE;
    else { server->send(400, "text/plain", "Unknown mode"); return; }

    AppMode oldMode = Mode::get();
    if (newMode == oldMode) { server->send(200, "text/plain", "OK"); return; }

    // Tear down current mode
    if (oldMode == AppMode::CENTRAL) {
      WsClient::stop();
    } else if (oldMode == AppMode::FIREBASE) {
      if (firebaseTaskHandle) {
        vTaskDelete(firebaseTaskHandle);
        firebaseTaskHandle = nullptr;
      }
    }

    Mode::set(newMode);

    // Start new mode
    if (newMode == AppMode::CENTRAL) {
      WsClient::init(WsClient::loadServerIp());
    } else if (newMode == AppMode::FIREBASE) {
      Firebase::loadPollInterval();
      xTaskCreatePinnedToCore(firebaseTask, "firebase", 8192, nullptr, 1, &firebaseTaskHandle, 0);
    }

    server->send(200, "text/plain", "OK");
  });

  server->on("/firebase/channel", HTTP_POST, []() {
    if (!server->hasArg("plain")) { server->send(400, "text/plain", "Bad"); return; }
    Firebase::setChannel(server->arg("plain"));
    server->send(200, "text/plain", "OK");
  });

  server->on("/firebase/pollinterval", HTTP_POST, []() {
    if (!server->hasArg("plain")) { server->send(400, "text/plain", "Bad"); return; }
    uint16_t secs = (uint16_t)constrain(server->arg("plain").toInt(), 1, 60);
    Firebase::setPollInterval(secs);
    server->send(200, "text/plain", "OK");
  });

  // OTA firmware update
  server->on("/update", HTTP_GET, []() {
    server->send_P(200, "text/html", OTA_HTML);
  });
  server->on("/update", HTTP_POST,
    []() {
      bool ok = !Update.hasError();
      server->sendHeader("Connection", "close");
      server->send(200, "text/plain", ok ? "OK" : Update.errorString());
      if (ok) { delay(100); ESP.restart(); }
    },
    []() {
      HTTPUpload& upload = server->upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("[OTA] Start: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
          Update.printError(Serial);
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) Serial.printf("[OTA] Success: %u bytes\n", upload.totalSize);
        else Update.printError(Serial);
      }
    }
  );

  // Check for firmware update via GitHub Releases API
  server->on("/update/check", HTTP_POST, []() {
    if (!WiFiMgr::isOnline()) {
      server->send(503, "application/json", "{\"error\":\"Board not online\"}");
      return;
    }
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, MANIFEST_URL);
    http.setTimeout(10000);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.addHeader("User-Agent", "roundnet-scoreboard");
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
      http.end();
      server->send(502, "application/json", "{\"error\":\"Fetch failed\"}");
      return;
    }
    // Filter to only deserialize what we need — keeps RAM usage low on large API responses
    JsonDocument filter;
    filter["tag_name"] = true;
    filter["body"]     = true;
    filter["assets"][0]["browser_download_url"] = true;
    JsonDocument doc;
    WiFiClient* stream = http.getStreamPtr();
    DeserializationError err = deserializeJson(doc, *stream, DeserializationOption::Filter(filter));
    http.end();
    if (err != DeserializationError::Ok) {
      server->send(502, "application/json", "{\"error\":\"Invalid response\"}");
      return;
    }
    // Strip leading 'v' from tag name ("v1.1.0" → "1.1.0")
    String latest = String(doc["tag_name"] | "?");
    if (latest.length() > 0 && (latest[0] == 'v' || latest[0] == 'V')) latest = latest.substring(1);
    String dlUrl  = String(doc["assets"][0]["browser_download_url"] | "");
    String notes  = String(doc["body"] | "");
    if (notes.length() > 200) notes = notes.substring(0, 200) + "...";
    String resp = "{";
    resp += "\"current\":\"" + String(FIRMWARE_VERSION) + "\",";
    resp += "\"latest\":\""  + latest + "\",";
    resp += "\"url\":\""     + dlUrl  + "\",";
    resp += "\"notes\":\""   + notes  + "\"";
    resp += "}";
    server->send(200, "application/json", resp);
  });

  // Start OTA download from URL (launched in background task, returns immediately)
  server->on("/update/fetch", HTTP_POST, []() {
    if (!WiFiMgr::isOnline()) {
      server->send(503, "application/json", "{\"ok\":false,\"error\":\"Board not online\"}");
      return;
    }
    String url = server->hasArg("plain") ? server->arg("plain") : "";
    url.trim();
    if (url.length() == 0 || url.length() >= sizeof(WsClient::_otaUrl)) {
      server->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid URL\"}");
      return;
    }
    strlcpy(WsClient::_otaUrl, url.c_str(), sizeof(WsClient::_otaUrl));
    xTaskCreate(WsClient::_otaTask, "ota_dl", 16384, nullptr, 2, nullptr);
    server->send(200, "application/json", "{\"ok\":true}");
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
  if (!ScoreActions::isDimActive()) LED::tick();

  bool online = WiFiMgr::isOnline();
  if (!_serviceOnline && online) {
    // Confirmed online — restore intended mode immediately.
    _offlineGraceStart = 0;
    _serviceOnline = true;
    AppMode intended = Mode::getIntended();
    if (intended == AppMode::CENTRAL) {
      Mode::setEffective(AppMode::CENTRAL);
      WsClient::init(WsClient::loadServerIp());
    } else if (intended == AppMode::FIREBASE) {
      Mode::setEffective(AppMode::FIREBASE);
      Firebase::loadPollInterval();
      xTaskCreatePinnedToCore(firebaseTask, "firebase", 8192, nullptr, 1, &firebaseTaskHandle, 0);
    }
  } else if (_serviceOnline && !online) {
    // Start or check grace period before tearing down — avoids reacting to brief hiccups.
    if (_offlineGraceStart == 0) _offlineGraceStart = millis();
    if ((millis() - _offlineGraceStart) >= OFFLINE_GRACE_MS) {
      _serviceOnline = false;
      _offlineGraceStart = 0;
      AppMode cur = Mode::get();
      if (cur == AppMode::CENTRAL) {
        WsClient::stop();
      } else if (cur == AppMode::FIREBASE) {
        if (firebaseTaskHandle) { vTaskDelete(firebaseTaskHandle); firebaseTaskHandle = nullptr; }
      }
      if (cur != AppMode::LOCAL) Mode::setEffective(AppMode::LOCAL);
    }
  } else {
    _offlineGraceStart = 0;  // online and stable — reset grace timer
  }
}

} // namespace Portal