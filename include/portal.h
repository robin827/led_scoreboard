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
#include "score_actions.h"
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

  <div class="scoreboard">
    <div class="rotate-notice" id="rotateNotice">&#8635; Players rotate positions</div>
    <div class="scores">
      <div class="set-badge set-badge-a" id="setA">0</div>
      <div class="set-badge set-badge-b" id="setB">0</div>
      <div id="scoreView" style="display:contents">
        <div class="team">
          <div class="team-label lbl-a first-srv" id="lblA" onclick="setFirstServer(0)">Team A<span class="srv-hint">1st</span></div>
          <div class="score-wrap">
            <div class="serve-col serve-col-a" id="serveA"></div>
            <div class="score score-a" id="scoreA">00</div>
          </div>
        </div>
        <div class="divider"></div>
        <div class="team">
          <div class="team-label lbl-b" id="lblB" onclick="setFirstServer(1)">Team B<span class="srv-hint">1st</span></div>
          <div class="score-wrap">
            <div class="score score-b" id="scoreB">00</div>
            <div class="serve-col serve-col-b" id="serveB"></div>
          </div>
        </div>
      </div>
      <div id="timerView" style="display:none;text-align:center;width:100%;padding:32px 0 8px">
        <div class="timer-val" id="timerText">3:00</div>
        <div class="timer-sub">Break</div>
      </div>
    </div>

    <div class="set-history" id="setHistory" style="display:none"></div>
    <div class="ratio-bar"><div class="ratio-a" id="ratioA"></div><div class="ratio-b" id="ratioB"></div></div>

    <div class="win-selector" id="winSelector">
      <span class="win-label">Play to</span>
      <button class="win-btn" id="wp11" data-wp="11" onclick="setWinPoints(11)">11</button>
      <button class="win-btn" id="wp15" data-wp="15" onclick="setWinPoints(15)">15</button>
      <button class="win-btn active" id="wp21" data-wp="21" onclick="setWinPoints(21)">21</button>
    </div>

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

  <div class="settings">
    <div class="setting-group">
      <label class="setting-label">Brightness</label>
      <input type="range" id="brightness" class="slider" min="1" max="255" value="80" oninput="setBrightness(this.value)">
      <div class="slider-value"><span id="brightVal">31</span>%</div>
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
      <label class="setting-label">LED Matrix</label>
      <div class="mode-selector">
        <button class="mode-btn" id="mat0" onclick="setMatrixSize(0)">24 × 8<span class="mode-sub">small</span></button>
        <button class="mode-btn" id="mat1" onclick="setMatrixSize(1)">32 × 16<span class="mode-sub">large</span></button>
      </div>
    </div>
  </div>

  <div class="settings">
    <div class="setting-group">
      <label class="setting-label">Mode</label>
      <div class="mode-selector">
        <button class="mode-btn" id="modeLocal" onclick="setMode(0)">Local<span class="mode-sub">standalone</span></button>
        <button class="mode-btn" id="modeRead" onclick="setMode(1)">Read<span class="mode-sub">cloud&nbsp;→&nbsp;board</span></button>
        <button class="mode-btn" id="modeWrite" onclick="setMode(2)">Write<span class="mode-sub">board&nbsp;→&nbsp;cloud</span></button>
      </div>
      <div class="mode-hint" id="modeHint" style="display:none"></div>
    </div>

    <div class="setting-group" id="channelGroup" style="display:none">
      <label class="setting-label">Channel (Match ID)</label>
      <select class="input" id="channel" onchange="saveChannel()">
        <option value="">— Select channel —</option>
        <option value="1">Match 1</option><option value="2">Match 2</option><option value="3">Match 3</option><option value="4">Match 4</option><option value="5">Match 5</option><option value="6">Match 6</option><option value="7">Match 7</option><option value="8">Match 8</option><option value="9">Match 9</option><option value="10">Match 10</option><option value="11">Match 11</option><option value="12">Match 12</option><option value="13">Match 13</option><option value="14">Match 14</option><option value="15">Match 15</option><option value="16">Match 16</option><option value="17">Match 17</option><option value="18">Match 18</option><option value="19">Match 19</option><option value="20">Match 20</option>
      </select>
    </div>

    <div class="setting-group" id="wifiGroup" style="display:none">
      <label class="setting-label">WiFi <span id="wifiStatus" class="status-badge status-offline">Offline</span></label>
      <button class="btn btn-scan" id="btnScan" onclick="scanWiFi()">Scan Networks</button>
      <div class="network-list" id="networkList" style="display:none"></div>
      <button class="btn-disconnect" id="btnDisconnect" style="display:none" onclick="disconnectWiFi()">Disconnect from <span id="connectedSSID"></span></button>
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

let currentMode = 0;
let _online = false;
let _isConnecting = false;
let _switching = false;
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

function updateTimerDisplay() {
  const remaining = Math.max(0, _localTimerEndMs - Date.now());
  const totalSec = Math.ceil(remaining / 1000);
  const m = Math.floor(totalSec / 60);
  const s = totalSec % 60;
  document.getElementById('timerText').textContent = m + ':' + String(s).padStart(2, '0');
  if (remaining <= 0) {
    clearInterval(_localTimerInterval); _localTimerInterval = null;
    document.getElementById('scoreView').style.display = 'contents';
    document.getElementById('timerView').style.display = 'none';
    document.querySelector('.scores').classList.remove('timer-active');
  }
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

function applyModeUI(mode, online) {
  document.getElementById('modeLocal').classList.toggle('active', mode === 0);
  document.getElementById('modeRead').classList.toggle('active', mode === 1);
  document.getElementById('modeWrite').classList.toggle('active', mode === 2);
  document.getElementById('channelGroup').style.display = mode !== 0 ? 'block' : 'none';
  document.getElementById('wifiGroup').style.display = mode !== 0 ? 'block' : 'none';
  const hint = document.getElementById('modeHint');
  const wifiLink = ' <a href="#" onclick="document.getElementById(\'wifiGroup\').scrollIntoView({behavior:\'smooth\'});return false" style="color:var(--a);text-decoration:none;font-weight:600">Connect below \u2193</a>';
  hint.style.display = 'block';
  if (mode === 1) hint.innerHTML = 'Read mode: scores are pulled from the cloud. A WiFi connection is required.' + (!online ? wifiLink : '');
  else if (mode === 2) hint.innerHTML = 'Write mode: scores are pushed on the cloud. Still works offline, syncs when connected.' + (!online ? wifiLink : '');
  else hint.innerHTML = 'Local mode: this phone communicates with the scoreboard locally. No internet required';
  const readMode = mode === 1;
  document.getElementById('controls').style.display = document.getElementById('actions').style.display = readMode ? 'none' : '';
  document.querySelectorAll('.team-label').forEach(el => el.style.cursor = readMode ? 'default' : 'pointer');
  const winSel = document.getElementById('winSelector');
  winSel.style.opacity = readMode ? '0.4' : '1';
  winSel.style.pointerEvents = readMode ? 'none' : '';
}

async function action(url, btn) {
  if (currentMode === 1) return;
  if (btn) { btn.classList.add('flash'); setTimeout(() => btn.classList.remove('flash'), 150); }
  try {
    const r = await fetch(url, {method:'POST'});
    if (r.status === 409) { showToast('Teams are even — no winner yet'); return; }
    refresh();
  } catch(e) {}
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
    document.getElementById('lblA').className = 'team-label lbl-a' + (d.firstServer === 0 ? ' first-srv' : '');
    document.getElementById('lblB').className = 'team-label lbl-b' + (d.firstServer === 1 ? ' first-srv' : '');

    document.getElementById('mat0').classList.toggle('active', !d.matrixLarge);
    document.getElementById('mat1').classList.toggle('active',  d.matrixLarge);

    _online = d.online;

    if (!_switching) {
      currentMode = d.mode;
      applyModeUI(d.mode, d.online);
    }

    if (d.boardId && !_boardIdDirty) {
      document.getElementById('boardId').value = d.boardId;
      document.getElementById('pageTitle').textContent = d.boardId;
    }
    if (d.channel) document.getElementById('channel').value = d.channel;
    if (d.winPoints) {
      document.querySelectorAll('.win-btn').forEach(b =>
        b.classList.toggle('active', parseInt(b.dataset.wp) === d.winPoints));
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

    // Break timer
    const timerSec = d.breakTimer || 0;
    if (timerSec > 0) {
      _localTimerEndMs = Date.now() + timerSec * 1000;
      if (!_localTimerInterval) {
        document.getElementById('scoreView').style.display = 'none';
        document.getElementById('timerView').style.display = 'block';
        document.querySelector('.scores').classList.add('timer-active');
        _localTimerInterval = setInterval(updateTimerDisplay, 200);
      }
      updateTimerDisplay();
    } else if (_localTimerInterval) {
      clearInterval(_localTimerInterval); _localTimerInterval = null;
      document.getElementById('scoreView').style.display = 'contents';
      document.getElementById('timerView').style.display = 'none';
      document.querySelector('.scores').classList.remove('timer-active');
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

  } catch(e) {}
}

async function setWinPoints(val) {
  document.querySelectorAll('.win-btn').forEach(b =>
    b.classList.toggle('active', parseInt(b.dataset.wp) === val));
  try { await fetch('/winpoints', {method:'POST', body: String(val)}); } catch(e) {}
}

function showPage(id) {
  document.querySelectorAll('.page').forEach(p => p.classList.toggle('active', p.id === id));
  document.body.classList.toggle('top-align', id === 'pageSettings');
}

async function setMatrixSize(v) {
  document.getElementById('mat0').classList.toggle('active', v === 0);
  document.getElementById('mat1').classList.toggle('active', v === 1);
  try { await fetch('/matrix', {method:'POST', body: String(v)}); refresh(); } catch(e) {}
}

async function setFirstServer(who) {
  if (currentMode === 1) return;
  try { await fetch('/serve/first', {method:'POST', body: String(who)}); refresh(); } catch(e) {}
}

async function setMode(mode) {
  _switching = true;
  currentMode = mode;
  applyModeUI(mode, _online);
  try {
    await fetch('/mode', {method:'POST', body: String(mode)});
  } catch(e) {}
  finally { _switching = false; }
  await refresh();
  if (mode !== 0 && !_online) {
    document.getElementById('wifiGroup').scrollIntoView({behavior:'smooth'});
  }
}

async function saveChannel() {
  try { await fetch('/channel', {method:'POST', body: document.getElementById('channel').value}); } catch(e) {}
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
    json += "\"mode\":" + String((int)Mode::get()) + ",";
    json += "\"channel\":\"" + Firebase::getChannel() + "\",";
    json += "\"brightness\":" + String(LED::getBrightness()) + ",";
    json += "\"matrixLarge\":" + String(LED::isMatrixLarge() ? "true" : "false") + ",";
    json += "\"online\":" + String(WiFiMgr::isOnline() ? "true" : "false") + ",";
    json += "\"ssid\":\"" + WiFiMgr::getSSID() + "\",";
    json += "\"rssi\":" + String(WiFiMgr::getRSSI()) + ",";
    json += "\"boardId\":\"" + WiFiMgr::getScoreboardId() + "\",";
    json += "\"breakTimer\":" + String(ScoreActions::breakTimerRemainingMs() / 1000) + ",";
    json += "\"rotations\":" + String(ScoreActions::getRotationCount()) + ",";
    json += "\"setsPlayed\":" + String(sSetA + sSetB) + ",";
    json += "\"histA\":[";
    for (int i = 0; i < sSetA + sSetB && i < 3; i++) { if (i) json += ","; json += String(sHistA[i]); }
    json += "],\"histB\":[";
    for (int i = 0; i < sSetA + sSetB && i < 3; i++) { if (i) json += ","; json += String(sHistB[i]); }
    json += "]";
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
  server->on("/reset",   HTTP_POST, []() { ScoreActions::apply("reset");   server->send(200, "text/plain", "OK"); });

  server->on("/serve/first", HTTP_POST, []() {
    String body = server->arg("plain");
    xSemaphoreTake(scoreMutex, portMAX_DELAY);
    currentScore.firstServer = (body == "1") ? 1 : 0;
    LED::update(currentScore);
    xSemaphoreGive(scoreMutex);
    server->send(200, "text/plain", "OK");
  });

  server->on("/winpoints", HTTP_POST, []() {
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

  // Matrix size
  server->on("/matrix", HTTP_POST, []() {
    if (server->hasArg("plain")) {
      bool large = (server->arg("plain") == "1");
      LED::setMatrix(large);
      xSemaphoreTake(scoreMutex, portMAX_DELAY);
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
  LED::tick();
}

} // namespace Portal