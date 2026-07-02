#pragma once
#include <Arduino.h>
#include "score.h"

namespace ScoreLogger {

struct Event {
  uint32_t t;                  // ms since game start (reset)
  uint8_t  sA, sB;             // current set scores
  uint8_t  setA, setB;         // sets won
  uint8_t  hA[3], hB[3];       // completed set history
  uint8_t  wp, hc, fmt;        // win points, hardcap, format
  uint8_t  fs;                 // firstServer (0=A, 1=B)
};

static constexpr uint16_t MAX_EVENTS = 250;
static Event    _ev[MAX_EVENTS];
static uint16_t _n       = 0;
static uint32_t _startMs = 0;

inline void reset() {
  _n       = 0;
  _startMs = millis();
}

inline void log(const Score& s) {
  if (_n >= MAX_EVENTS) return;
  Event& e = _ev[_n++];
  e.t    = millis() - _startMs;
  e.sA   = s.scoreA;    e.sB   = s.scoreB;
  e.setA = s.setA;      e.setB = s.setB;
  memcpy(e.hA, s.histA, 3);
  memcpy(e.hB, s.histB, 3);
  e.wp   = s.winPoints; e.hc   = s.hardcap;  e.fmt  = s.format;
  e.fs   = s.firstServer;
}

inline String toJson() {
  String out;
  out.reserve((uint16_t)(_n * 85 + 4));
  out = '[';
  for (uint16_t i = 0; i < _n; i++) {
    if (i) out += ',';
    const Event& e = _ev[i];
    out += F("{\"t\":");   out += e.t;
    out += F(",\"sA\":"); out += e.sA;
    out += F(",\"sB\":"); out += e.sB;
    out += F(",\"setA\":"); out += e.setA;
    out += F(",\"setB\":"); out += e.setB;
    out += F(",\"hA\":["); out += e.hA[0]; out += ','; out += e.hA[1]; out += ','; out += e.hA[2]; out += ']';
    out += F(",\"hB\":["); out += e.hB[0]; out += ','; out += e.hB[1]; out += ','; out += e.hB[2]; out += ']';
    out += F(",\"wp\":"); out += e.wp;
    out += F(",\"hc\":"); out += e.hc;
    out += F(",\"fmt\":"); out += e.fmt;
    out += F(",\"fs\":"); out += e.fs;
    out += '}';
  }
  out += ']';
  return out;
}

} // namespace ScoreLogger
