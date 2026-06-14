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

// Defined in main.cpp — forward declarations so the /mode route can manage the Firebase task
extern TaskHandle_t firebaseTaskHandle;
extern void firebaseTask(void*);

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
      <div class="setting-group" style="margin-bottom:0">
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
    </div>
  </details>

  <div class="settings" style="margin-top:12px">
    <div class="setting-group" style="margin:0">
      <button class="btn" style="width:100%;background:rgba(var(--b-rgb),0.1);color:var(--b);border:1px solid rgba(var(--b-rgb),0.3)"
        onclick="openModal('Sleep','Put the scoreboard to sleep?','modal-ok-red','/sleepnow')">Sleep</button>
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

  <div class="settings">
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
      <label class="setting-label">WiFi <span id="wifiStatus" class="status-badge status-offline">Offline</span></label>
      <button class="btn btn-scan" id="btnScan" onclick="scanWiFi()">Scan Networks</button>
      <div class="network-list" id="networkList" style="display:none"></div>
      <button class="btn-disconnect" id="btnDisconnect" style="display:none" onclick="disconnectWiFi()">Disconnect from <span id="connectedSSID"></span></button>
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

setInterval(refresh, 2000);
document.addEventListener('visibilitychange', () => { if (!document.hidden) refresh(); });
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
}

} // namespace Portal