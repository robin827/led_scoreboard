/**
 * led.h - LED display supporting 24×8 (small) and 32×16 (large) matrices
 */

#pragma once
#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include "config.h"
#include "score.h"

namespace LED {

// Allocate for the largest matrix (32×16 = 512); sending extra data to 24×8 is harmless
static CRGB        _leds[512];
static bool        _matrixLarge = false;
static Preferences _prefs;

// ─── Small matrix (24×8) ─────────────────────────────────────────────────────
// Zigzag vertical, starts bottom-right. Even cols (from right): bottom→top.

inline int xy(int x, int y) {
  int col = (Config::NUM_COLS - 1) - x;
  int row = (col % 2 == 0) ? (Config::NUM_ROWS - 1) - y : y;
  return col * Config::NUM_ROWS + row;
}

static const uint8_t DIGITS[10][7][4] = {
  {{0,1,1,0},{1,0,0,1},{1,0,0,1},{1,0,0,1},{1,0,0,1},{1,0,0,1},{0,1,1,0}}, // 0
  {{0,0,1,0},{0,1,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,1,1,1}}, // 1
  {{0,1,1,0},{1,0,0,1},{0,0,0,1},{0,0,1,0},{0,1,0,0},{1,0,0,0},{1,1,1,1}}, // 2
  {{0,1,1,0},{1,0,0,1},{0,0,0,1},{0,1,1,0},{0,0,0,1},{1,0,0,1},{0,1,1,0}}, // 3
  {{0,0,1,0},{0,1,1,0},{1,0,1,0},{1,0,1,0},{1,1,1,1},{0,0,1,0},{0,0,1,0}}, // 4
  {{1,1,1,1},{1,0,0,0},{1,1,1,0},{0,0,0,1},{0,0,0,1},{1,0,0,1},{0,1,1,0}}, // 5
  {{0,1,1,0},{1,0,0,0},{1,0,0,0},{1,1,1,0},{1,0,0,1},{1,0,0,1},{0,1,1,0}}, // 6
  {{1,1,1,1},{0,0,0,1},{0,0,1,0},{0,0,1,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}}, // 7
  {{0,1,1,0},{1,0,0,1},{1,0,0,1},{0,1,1,0},{1,0,0,1},{1,0,0,1},{0,1,1,0}}, // 8
  {{0,1,1,0},{1,0,0,1},{1,0,0,1},{0,1,1,1},{0,0,0,1},{0,0,0,1},{0,1,1,0}}  // 9
};

inline void drawDigit(int d, int sx, int sy, CRGB col) {
  if (d < 0 || d > 9) return;
  for (int y = 0; y < 7; y++)
    for (int x = 0; x < 4; x++)
      if (DIGITS[d][y][x]) {
        int px = sx + x, py = sy + y;
        if (px >= 0 && px < Config::NUM_COLS && py >= 0 && py < Config::NUM_ROWS)
          _leds[xy(px, py)] = col;
      }
}

// ─── Large matrix (32×16) ────────────────────────────────────────────────────
// Column-major snake: even cols top→bottom, odd cols bottom→top.

static constexpr uint8_t L_COLS = 32;
static constexpr uint8_t L_ROWS = 16;

inline int xyLarge(int x, int y) {
  return (x % 2 == 0) ? x * L_ROWS + y : (x + 1) * L_ROWS - 1 - y;
}

// 6×11 pixel digit shapes (x: 0-5, y: 0-10), transcribed from user-provided font
static const int8_t _LD0[][2] = {
  {1,0},{2,0},{3,0},{4,0},{5,0},
  {1,1},{5,1},{1,2},{5,2},{1,3},{5,3},{1,4},{5,4},
  {1,5},{5,5},{1,6},{5,6},{1,7},{5,7},{1,8},{5,8},{1,9},{5,9},
  {1,10},{2,10},{3,10},{4,10},{5,10}
};
static const int8_t _LD1[][2] = {
  {2,1},{1,2},
  {3,0},{3,1},{3,2},{3,3},{3,4},{3,5},{3,6},{3,7},{3,8},{3,9},{3,10},
  {1,10},{2,10},{4,10},{5,10}
};
static const int8_t _LD2[][2] = {
  {0,0},{1,0},{2,0},{3,0},{4,0},{5,0},
  {5,1},{5,2},{5,3},{5,4},
  {1,5},{2,5},{3,5},{4,5},{5,5},
  {0,5},{0,6},{0,7},{0,8},{0,9},{0,10},
  {0,10},{1,10},{2,10},{3,10},{4,10},{5,10}
};
static const int8_t _LD3[][2] = {
  {0,0},{1,0},{2,0},{3,0},{4,0},{5,0},
  {5,1},{5,2},{5,3},{5,4},{5,5},{5,6},{5,7},{5,8},{5,9},
  {1,5},{2,5},{3,5},{4,5},{5,5},
  {0,10},{1,10},{2,10},{3,10},{4,10},{5,10}
};
static const int8_t _LD4[][2] = {
  {0,0},{0,1},{0,2},{0,3},{0,4},{0,5},
  {5,0},{5,1},{5,2},{5,3},{5,4},{5,5},{5,6},{5,7},{5,8},{5,9},{5,10},
  {1,5},{2,5},{3,5},{4,5}
};
static const int8_t _LD5[][2] = {
  {0,0},{1,0},{2,0},{3,0},{4,0},{5,0},
  {0,1},{0,2},{0,3},{0,4},
  {0,5},{1,5},{2,5},{3,5},{4,5},{5,5},
  {5,6},{5,7},{5,8},{5,9},{5,10},
  {0,10},{1,10},{2,10},{3,10},{4,10},{5,10}
};
static const int8_t _LD6[][2] = {
  {0,0},{1,0},{2,0},{3,0},{4,0},{5,0},
  {0,1},{0,2},{0,3},{0,4},{0,5},{0,6},{0,7},{0,8},{0,9},{0,10},
  {1,5},{2,5},{3,5},{4,5},{5,5},
  {5,6},{5,7},{5,8},{5,9},{5,10},
  {1,10},{2,10},{3,10},{4,10},{5,10}
};
static const int8_t _LD7[][2] = {
  {0,0},{1,0},{2,0},{3,0},{4,0},{5,0},
  {5,1},{5,2},{5,3},{5,4},{5,5},{5,6},{5,7},{5,8},{5,9},{5,10}
};
static const int8_t _LD8[][2] = {
  {0,0},{1,0},{2,0},{3,0},{4,0},{5,0},
  {0,1},{0,2},{0,3},{0,4},
  {5,1},{5,2},{5,3},{5,4},
  {0,5},{1,5},{2,5},{3,5},{4,5},{5,5},
  {0,6},{0,7},{0,8},{0,9},{0,10},
  {5,6},{5,7},{5,8},{5,9},{5,10},
  {1,10},{2,10},{3,10},{4,10}
};
static const int8_t _LD9[][2] = {
  {0,0},{1,0},{2,0},{3,0},{4,0},{5,0},
  {0,1},{0,2},{0,3},{0,4},{0,5},
  {5,1},{5,2},{5,3},{5,4},{5,5},{5,6},{5,7},{5,8},{5,9},{5,10},
  {1,5},{2,5},{3,5},{4,5},
  {0,10},{1,10},{2,10},{3,10},{4,10}
};

// 3×4 small digit shapes for set scores
static const int8_t _SD0[][2] = {
  {0,0},{1,0},{2,0},
  {0,1},{2,1},
  {0,2},{2,2},
  {0,3},{1,3},{2,3}
};
static const int8_t _SD1[][2] = {
  {1,0},{1,1},{1,2},{1,3}
};
static const int8_t _SD2[][2] = {
  {0,0},{1,0},{2,0},
  {2,1},
  {1,2},
  {0,3},{1,3},{2,3}
};

// maxRelY: skip pixels at relative y >= maxRelY (used to trim digit height)
template<size_t N>
inline void _drawLarge(const int8_t (&c)[N][2], int sx, int sy, CRGB col, int8_t maxRelY = 127) {
  for (size_t i = 0; i < N; i++) {
    if (c[i][1] >= maxRelY) continue;
    int x = c[i][0] + sx, y = c[i][1] + sy;
    if (x >= 0 && x < L_COLS && y >= 0 && y < L_ROWS)
      _leds[xyLarge(x, y)] = col;
  }
}

inline void drawDigitSmall(int d, int sx, int sy, CRGB col) {
  switch (constrain(d, 0, 2)) {
    case 0: _drawLarge(_SD0, sx, sy, col); break;
    case 1: _drawLarge(_SD1, sx, sy, col); break;
    case 2: _drawLarge(_SD2, sx, sy, col); break;
  }
}

inline void drawDigitLarge(int d, int sx, int sy, CRGB col) {
  switch (d) {
    case 0: _drawLarge(_LD0, sx, sy, col); break;
    case 1: _drawLarge(_LD1, sx, sy, col); break;
    case 2: _drawLarge(_LD2, sx, sy, col); break;
    case 3: _drawLarge(_LD3, sx, sy, col); break;
    case 4: _drawLarge(_LD4, sx, sy, col); break;
    case 5: _drawLarge(_LD5, sx, sy, col); break;
    case 6: _drawLarge(_LD6, sx, sy, col); break;
    case 7: _drawLarge(_LD7, sx, sy, col); break;
    case 8: _drawLarge(_LD8, sx, sy, col); break;
    case 9: _drawLarge(_LD9, sx, sy, col); break;
  }
}

// ─── Breathing animation state ───────────────────────────────────────────────

static Score    _animScore;
static uint32_t _lastTick = 0;

// Returns a brightness multiplier (70–255) that oscillates ~once per 2 s
inline uint8_t _breatheFactor() {
  // sin8(x) returns 0-255; we map it to 70-255 so the LED never goes fully dark
  return (uint8_t)(20 + scale8(sin8((uint8_t)(millis() / 8)), 185));
}

// ─── Serve pixel helpers (called with optional breathe scale) ────────────────

inline void _applyServeSmall(const Score& score, uint8_t breathe) {
  if (isSetWon(score)) return;
  CRGB colorA = CRGB(255, 68, 0);
  CRGB colorB = CRGB(0, 136, 255);
  ServeInfo srv = getServeInfo(score);
  int sc = srv.teamAServing ? 0 : 23;
  CRGB bright = srv.teamAServing ? colorA : colorB;
  bright.nscale8(breathe);
  CRGB dim = srv.teamAServing ? colorA : colorB; dim.nscale8(100);
  if (srv.serveTotal == 2) {
    _leds[xy(sc, 2)] = (srv.servesLeft == 2) ? bright : dim;
    _leds[xy(sc, 5)] = bright;
  } else {
    _leds[xy(sc, 3)] = bright;
  }
}

inline void _applyServeLarge(const Score& score, uint8_t breathe) {
  if (isSetWon(score)) return;
  CRGB colorA = CRGB(255, 68, 0);
  CRGB colorB = CRGB(0, 136, 255);
  ServeInfo srv = getServeInfo(score);
  int sc = srv.teamAServing ? 0 : 31;
  CRGB bright = srv.teamAServing ? colorA : colorB;
  bright.nscale8(breathe);
  CRGB dim = srv.teamAServing ? colorA : colorB; dim.nscale8(100);
  if (srv.serveTotal == 2) {
    _leds[xyLarge(sc, 4)]  = (srv.servesLeft == 2) ? bright : dim;
    _leds[xyLarge(sc, 10)] = bright;
  } else {
    _leds[xyLarge(sc, 10)] = bright;
  }
}

// ─── Render functions ────────────────────────────────────────────────────────

inline void _updateSmall(const Score& score, uint8_t breathe) {
  CRGB colorA = CRGB(255, 68, 0);
  CRGB colorB = CRGB(0, 136, 255);

  // Digit "1" visual center is 0.5px right of cell center; shift Team A left by 1 to keep display symmetric
  int dAs_t = score.scoreA / 10, dAs_u = score.scoreA % 10;
  drawDigit(dAs_t, 2 + (dAs_t == 1 ? -1 : 0), 0, colorA);
  drawDigit(dAs_u, 7 + (dAs_u == 1 ? -1 : 0), 0, colorA);
  drawDigit(score.scoreB / 10, 13, 0, colorB);
  drawDigit(score.scoreB % 10, 18, 0, colorB);

  if (score.setA > 0) _leds[xy(1, 7)] = colorA;
  if (score.setA > 1) _leds[xy(2, 7)] = colorA;
  if (score.setB > 0) _leds[xy(21, 7)] = colorB;
  if (score.setB > 1) _leds[xy(22, 7)] = colorB;

  _applyServeSmall(score, breathe);
}

// Large matrix layout (32 cols × 16 rows):
//  x=0        — serve indicator (Team A)
//  x=2…7      — Team A tens digit
//  x=9…14     — Team A units digit
//  x=17…22    — Team B tens digit
//  x=24…29    — Team B units digit
//  x=31       — serve indicator (Team B)
//  y=2…12     — digits (11 rows, centred in 16)
//  y=14       — set badges
inline void _updateLarge(const Score& score, uint8_t breathe) {
  CRGB colorA = CRGB(255, 68, 0);
  CRGB colorB = CRGB(0, 136, 255);

  // dy=0: digits occupy y=0-10, y=11 is a free gap, set scores at y=12-15
  const int dy = 0;
  // Digits 0 and 1 have visual content at x=1..5 (center x=3) vs x=0..5 (center x=2.5) for other digits.
  // Shift Team A left by 1 for those digits so the overall display stays symmetric.
  int dAl_t = score.scoreA / 10, dAl_u = score.scoreA % 10;
  auto ATensOff = [](int d) -> int { return (d >= 2) ? 1 : 0; };
  auto AUnitsOff = [](int d) -> int { return (d >= 2) ? 1 : 0; };
  drawDigitLarge(dAl_t, 1 + ATensOff(dAl_t), dy, colorA);
  drawDigitLarge(dAl_u, 8+ AUnitsOff(dAl_t), dy, colorA);
  int dBl_t = score.scoreB / 10, dBl_u = score.scoreB % 10;
  drawDigitLarge(dBl_t, 17, dy, colorB);
  drawDigitLarge(dBl_u, 24, dy, colorB);

  // Set scores — 3×4 small digits at bottom (y=12-15)
  // Team A at x=1, Team B at x=28 (clear of serve indicator columns 0 and 31)
  drawDigitSmall(score.setA, 1,  12, colorA);
  drawDigitSmall(score.setB, 28, 12, colorB);

  _applyServeLarge(score, breathe);
}

// ─── Public API ──────────────────────────────────────────────────────────────

inline bool isMatrixLarge() { return _matrixLarge; }

inline void setMatrix(bool large) {
  _matrixLarge = large;
  _prefs.begin("led", false);
  _prefs.putBool("matLarge", large);
  _prefs.end();
  Serial.printf("[LED] Matrix: %s\n", large ? "32x16" : "24x8");
}

inline void init() {
  // Always add 512 LEDs — extra data is harmless for the 24×8 strip
  FastLED.addLeds<WS2812B, Config::PIN, GRB>(_leds, 512);

  _prefs.begin("led", true);
  uint8_t brightness = _prefs.getUChar("brightness", Config::BRIGHTNESS);
  _matrixLarge       = _prefs.getBool("matLarge", false);
  _prefs.end();

  FastLED.setBrightness(brightness);
  FastLED.clear();
  FastLED.show();
  Serial.printf("[LED] Init: %s, brightness=%d\n", _matrixLarge ? "32x16" : "24x8", brightness);
}

inline void setBrightness(uint8_t brightness) {
  brightness = constrain(brightness, 1, 255);
  FastLED.setBrightness(brightness);
  FastLED.show();
  _prefs.begin("led", false);
  _prefs.putUChar("brightness", brightness);
  _prefs.end();
  Serial.printf("[LED] Brightness: %d\n", brightness);
}

inline uint8_t getBrightness() { return FastLED.getBrightness(); }

inline void update(const Score& score) {
  _animScore = score;
  FastLED.clear();
  if (_matrixLarge) _updateLarge(score, _breatheFactor());
  else              _updateSmall(score, _breatheFactor());
  FastLED.show();
}

// Call from main loop at high frequency; animates serve pixels at ~30 fps
inline void tick() {
  uint32_t now = millis();
  if (now - _lastTick < 33) return;  // ~30 fps
  _lastTick = now;
  if (_matrixLarge) _applyServeLarge(_animScore, _breatheFactor());
  else              _applyServeSmall(_animScore, _breatheFactor());
  FastLED.show();
}

} // namespace LED
