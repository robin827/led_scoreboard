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

static const CRGB COLOR_A = CRGB(255, 100, 0);
static const CRGB COLOR_B = CRGB::Cyan;

// ─── Small matrix (24×12 extended) ───────────────────────────────────────────
// Original 24×8 panel: column-major zigzag, bottom-right origin (LEDs 0–191).
// Extension: single 24×4 panel (row-major horizontal snake, top-left origin).
//   y=8  x=0→23   LEDs 192–215
//   y=9  x=23→0   LEDs 216–239
//   y=10 x=0→23   LEDs 240–263
//   y=11 x=23→0   LEDs 264–287

inline int xy(int x, int y) {
  if (y < Config::NUM_ROWS) {
    int col = (Config::NUM_COLS - 1) - x;
    int row = (col % 2 == 0) ? (Config::NUM_ROWS - 1) - y : y;
    return col * Config::NUM_ROWS + row;
  }
  // Extension: column-major zigzag, top-left origin (same logic as main panel, no x-flip)
  // Even cols (from left): top→bottom. Odd cols: bottom→top.
  int r = y - Config::NUM_ROWS;       // 0..3 within extension
  int led = (x % 2 == 0) ? x * 4 + r : x * 4 + (3 - r);
  return 192 + led;
}

static const uint8_t DIGITS[10][7][4] = {
  {{0,1,1,0},{1,0,0,1},{1,0,0,1},{1,0,0,1},{1,0,0,1},{1,0,0,1},{0,1,1,0}}, // 0  — drop one side row
  {{0,0,1,0},{0,1,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,1,1,1}}, // 1  — drop one middle stroke row
  {{0,1,1,0},{1,0,0,1},{0,0,0,1},{0,0,1,0},{0,1,0,0},{1,0,0,0},{1,1,1,1}}, // 2  — drop X... row (least structural)
  {{0,1,1,0},{1,0,0,1},{0,0,0,1},{0,1,1,0},{0,0,0,1},{1,0,0,1},{0,1,1,0}}, // 3  — drop right-side row, middle bar at row 2
  {{0,0,1,0},{0,1,1,0},{1,0,1,0},{1,0,1,0},{1,1,1,1},{0,0,1,0},{0,0,1,0}}, // 4  — drop one X·X· row
  {{1,1,1,1},{1,0,0,0},{1,0,0,0},{1,1,1,0},{0,0,0,1},{1,0,0,1},{0,1,1,0}}, // 5  — drop one ···X row
  {{0,1,1,0},{1,0,0,0},{1,0,0,0},{1,1,1,0},{1,0,0,1},{1,0,0,1},{0,1,1,0}}, // 6  — drop one X··· row
  {{1,1,1,1},{0,0,0,1},{0,0,0,1},{0,0,1,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}}, // 7  — drop one diagonal step
  {{0,1,1,0},{1,0,0,1},{1,0,0,1},{0,1,1,0},{1,0,0,1},{1,0,0,1},{0,1,1,0}}, // 8  — drop one side row from top half
  {{0,1,1,0},{1,0,0,1},{1,0,0,1},{0,1,1,1},{0,0,0,1},{0,0,0,1},{0,1,1,0}}  // 9  — drop one side row from top loop
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
  {1,0},
  {0,1},{1,1},
  {1,2},
  {0,3}, {1,3}, {2,3}
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

// Same 3×4 glyphs rendered onto the small matrix (uses xy, not xyLarge)
template<size_t N>
inline void _drawSm(const int8_t (&c)[N][2], int sx, int sy, CRGB col) {
  for (size_t i = 0; i < N; i++) {
    int x = c[i][0] + sx, y = c[i][1] + sy;
    if (x >= 0 && x < Config::NUM_COLS && y >= 0 && y < Config::NUM_ROWS + 4)
      _leds[xy(x, y)] = col;
  }
}

inline void drawSetDigitSm(int d, int sx, int sy, CRGB col) {
  switch (constrain(d, 0, 2)) {
    case 0: _drawSm(_SD0, sx, sy, col); break;
    case 1: _drawSm(_SD1, sx, sy, col); break;
    case 2: _drawSm(_SD2, sx, sy, col); break;
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
static uint32_t _lastTick  = 0;
static bool     _timerMode = false;  // suppresses serve-dot tick during break timer

// Returns a brightness multiplier (0–255) that oscillates ~once per 1.3 s, reaching full off
inline uint8_t _breatheFactor() {
  return sin8((uint8_t)(millis() / 5));
}

// ─── Serve pixel helpers (called with optional breathe scale) ────────────────

inline void _applyServeSmall(const Score& score, uint8_t breathe) {
  if (isSetWon(score)) return;
  ServeInfo srv = getServeInfo(score);
  int sc = srv.teamAServing ? 0 : 23;
  CRGB base = srv.teamAServing ? COLOR_A : COLOR_B;
  CRGB breathing = base; breathing.nscale8(breathe);
  if (srv.serveTotal == 2 && srv.servesLeft == 2) {
    // First serve: top breathes, bottom solid
    _leds[xy(sc, 0)] = breathing; _leds[xy(sc, 1)] = breathing; _leds[xy(sc, 2)] = breathing;
    _leds[xy(sc, 5)] = base;      _leds[xy(sc, 6)] = base;      _leds[xy(sc, 7)] = base;
  } else if (srv.serveTotal == 2 && srv.servesLeft == 1) {
    // Second serve: top solid, bottom breathes
    _leds[xy(sc, 0)] = base;      _leds[xy(sc, 1)] = base;      _leds[xy(sc, 2)] = base;
    _leds[xy(sc, 5)] = breathing; _leds[xy(sc, 6)] = breathing; _leds[xy(sc, 7)] = breathing;
  } else {
    // Deuce: bottom breathes only
    _leds[xy(sc, 5)] = breathing; _leds[xy(sc, 6)] = breathing; _leds[xy(sc, 7)] = breathing;
  }
}

inline void _applyServeLarge(const Score& score, uint8_t breathe) {
  if (isSetWon(score)) return;
  ServeInfo srv = getServeInfo(score);
  int sc = srv.teamAServing ? 0 : 31;
  CRGB base = srv.teamAServing ? COLOR_A : COLOR_B;
  CRGB breathing = base; breathing.nscale8(breathe);
  if (srv.serveTotal == 2 && srv.servesLeft == 2) {
    // First serve: top breathes, bottom solid
    _leds[xyLarge(sc, 0)] = breathing; _leds[xyLarge(sc, 1)] = breathing; _leds[xyLarge(sc, 2)] = breathing;
    _leds[xyLarge(sc, 8)] = base;      _leds[xyLarge(sc, 9)] = base;      _leds[xyLarge(sc, 10)] = base;
  } else if (srv.serveTotal == 2 && srv.servesLeft == 1) {
    // Second serve: top solid, bottom breathes
    _leds[xyLarge(sc, 0)] = base;      _leds[xyLarge(sc, 1)] = base;      _leds[xyLarge(sc, 2)] = base;
    _leds[xyLarge(sc, 8)]  = breathing; _leds[xyLarge(sc, 9)]  = breathing; _leds[xyLarge(sc, 10)] = breathing;
  } else {
    // Deuce: bottom breathes only
    _leds[xyLarge(sc, 8)]  = breathing; _leds[xyLarge(sc, 9)]  = breathing; _leds[xyLarge(sc, 10)] = breathing;
  }
}

// ─── Render functions ────────────────────────────────────────────────────────

inline void _updateSmall(const Score& score, uint8_t breathe) {
  // Digit "1" visual center is 0.5px right of cell center; shift Team A left by 1 to keep display symmetric
  int dAs_t = score.scoreA / 10, dAs_u = score.scoreA % 10;
  drawDigit(dAs_t, 2 + (dAs_t == 1 ? -1 : 0), 0, COLOR_A);
  drawDigit(dAs_u, 7 + (dAs_u == 1 ? -1 : 0), 0, COLOR_A);
  drawDigit(score.scoreB / 10, 13, 0, COLOR_B);
  drawDigit(score.scoreB % 10, 18, 0, COLOR_B);

  // Set scores: 3×4 digit glyphs in the extension rows (y=8..11)
  // x=2 (Team A) and x=19 (Team B) give 2-pixel margins from each serve column
  drawSetDigitSm(score.setA, 5,  Config::NUM_ROWS, COLOR_A);
  drawSetDigitSm(score.setB, 16, Config::NUM_ROWS, COLOR_B);

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
  // dy=0: digits occupy y=0-10, y=11 is a free gap, set scores at y=12-15
  const int dy = 0;
  // Digits 0 and 1 have visual content at x=1..5 (center x=3) vs x=0..5 (center x=2.5) for other digits.
  // Shift Team A left by 1 for those digits so the overall display stays symmetric.
  int dAl_t = score.scoreA / 10, dAl_u = score.scoreA % 10;
  auto ATensOff = [](int d) -> int { return (d >= 2) ? 1 : 0; };
  auto AUnitsOff = [](int d) -> int { return (d >= 2) ? 1 : 0; };
  drawDigitLarge(dAl_t, 1 + ATensOff(dAl_t), dy, COLOR_A);
  drawDigitLarge(dAl_u, 8+ AUnitsOff(dAl_t), dy, COLOR_A);
  int dBl_t = score.scoreB / 10, dBl_u = score.scoreB % 10;
  drawDigitLarge(dBl_t, 17, dy, COLOR_B);
  drawDigitLarge(dBl_u, 24, dy, COLOR_B);

  // Set scores — 3×4 small digits at bottom (y=12-15)
  // Team A at x=1, Team B at x=28 (clear of serve indicator columns 0 and 31)
  drawDigitSmall(score.setA, 2,  12, COLOR_A);
  drawDigitSmall(score.setB, 27, 12, COLOR_B);

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
  FastLED.addLeds<WS2812B, Config::PIN, GRB>(_leds, 512);

  _prefs.begin("led", false);
  uint8_t brightness = _prefs.getUChar("brightness", Config::BRIGHTNESS);
  _matrixLarge       = _prefs.getBool("matLarge", false);
  _prefs.end();

  FastLED.setBrightness(brightness);
  FastLED.clear();
  FastLED.show();
  Serial.printf("[LED] Init: %s, brightness=%d\n", _matrixLarge ? "32x16" : "24x8", brightness);
}

inline void bootAnimation() {
  const int cols = _matrixLarge ? L_COLS : (int)Config::NUM_COLS;
  const int rows = _matrixLarge ? L_ROWS : (int)(Config::NUM_ROWS + 4);  // 24×12 incl. extension

  const float cx   = (cols - 1) / 2.0f;
  const float cy   = (rows - 1) / 2.0f;
  const float maxD = sqrtf(cx * cx + cy * cy);
  const float PI_F = 3.14159265f;

  auto setPixel = [&](int x, int y, CRGB c) {
    if (_matrixLarge) _leds[xyLarge(x, y)] = c;
    else              _leds[xy(x, y)] = c;
  };

  // Brief white spark at center before explosion
  FastLED.clear();
  setPixel((int)roundf(cx), (int)roundf(cy), CRGB::White);
  FastLED.show();
  delay(80);

  // Expanding rainbow ring: each pixel lights up when the wavefront reaches it.
  // Color = angle around center (full hue wheel). Behind the ring: sparks decay.
  const int   STEPS     = 35;
  const float beamWidth = 2.0f;

  for (int step = 0; step < STEPS + 20; step++) {
    float radius = ((float)step / STEPS) * (maxD + beamWidth);

    for (int x = 0; x < cols; x++) {
      for (int y = 0; y < rows; y++) {
        float dx   = x - cx;
        float dy   = y - cy;
        float dist = sqrtf(dx * dx + dy * dy);
        float diff = radius - dist;  // >0 = wavefront has passed this pixel

        int idx = _matrixLarge ? xyLarge(x, y) : xy(x, y);

        if (diff < 0.0f) {
          _leds[idx] = CRGB::Black;                       // not yet reached
        } else if (diff < beamWidth) {
          float   bri_f = 1.0f - diff / beamWidth;        // brightest at leading edge
          uint8_t bri   = (uint8_t)(bri_f * 255.0f);
          uint8_t hue   = (uint8_t)((atan2f(dy, dx) + PI_F) / (2.0f * PI_F) * 255.0f);
          _leds[idx]    = CHSV(hue, 220, bri);
        } else {
          _leds[idx].nscale8(200);                         // spark decay behind ring
        }
      }
    }

    FastLED.show();
    delay(22);
  }

  // Fade to black
  for (int step = 0; step < 20; step++) {
    for (int i = 0; i < 512; i++) _leds[i].nscale8(195);
    FastLED.show();
    delay(18);
  }

  FastLED.clear();
  FastLED.show();
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

inline void setRawBrightness(uint8_t brightness) {
  FastLED.setBrightness(constrain(brightness, 1, 255));
  FastLED.show();
}

inline void update(const Score& score) {
  _animScore = score;
  _timerMode = false;
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
  if (_timerMode) return;  // timer display is managed by main loop
  if (_matrixLarge) _applyServeLarge(_animScore, _breatheFactor());
  else              _applyServeSmall(_animScore, _breatheFactor());
  FastLED.show();
}

// 4 players (2 team A, 2 team B) rotating CCW by a quarter circle over 2 seconds.
// Players start at 0°/90°/180°/270° on a circle; each moves +90° CCW.
// Screen convention: x = cx + r*cos(a),  y = cy - r*sin(a)  (y flipped).
inline void rotationAnimation() {
  const int cols = _matrixLarge ? L_COLS : (int)Config::NUM_COLS;
  const int rows = _matrixLarge ? L_ROWS : (int)(Config::NUM_ROWS + 4);  // full 24×12
  const float cx = (cols - 1) * 0.5f;
  const float cy = (rows - 1) * 0.5f;
  const float r  = _matrixLarge ? 5.5f : 5.5f;  // fills 24×12: min(cx=11.5, cy=5.5)
  static constexpr float PI2 = 6.28318530f;

  auto sp = [&](int x, int y, CRGB c) {
    if (x < 0 || x >= cols || y < 0 || y >= rows) return;
    if (_matrixLarge) _leds[xyLarge(x, y)] = c;
    else              _leds[xy(x, y)]       = c;
  };

  // A1 at 0°(right), A2 at 90°(top), B1 at 180°(left), B2 at 270°(bottom)
  const float startAngle[4] = { 0.0f, PI2*0.25f, PI2*0.5f, PI2*0.75f };
  const CRGB  playerCol[4]  = { COLOR_A, COLOR_A, COLOR_B, COLOR_B };
  const float netR       = _matrixLarge ? 2.5f : 1.5f;
  const int   NET_STEPS  = 12;
  const int   FRAMES     = 25;    // frames per rep (500 ms at 20 ms/frame)
  const int   REPS       = 3;
  const int   PAUSE_MS   = 120;   // pause between reps (white circle only)
  const int   TRAIL      = 4;
  const float TRAIL_STEP = 0.12f;

  auto drawNet = [&]() {
    for (int i = 0; i < NET_STEPS; i++) {
      float a = PI2 * i / NET_STEPS;
      sp((int)roundf(cx + netR * cosf(a)), (int)roundf(cy - netR * sinf(a)), CRGB(200, 200, 200));
    }
  };

  for (int rep = 0; rep < REPS; rep++) {
    for (int f = 0; f < FRAMES; f++) {
      FastLED.clear();
      drawNet();
      float rot = PI2 * 0.25f * f / (FRAMES - 1);  // 0 → 90° CCW
      for (int p = 0; p < 4; p++) {
        float head = startAngle[p] + rot;
        for (int t = TRAIL; t >= 0; t--) {
          float a  = head - t * TRAIL_STEP;
          int   px = (int)roundf(cx + r * cosf(a));
          int   py = (int)roundf(cy - r * sinf(a));
          uint8_t bri = (t == 0) ? 230 : (uint8_t)(40u + 50u * (TRAIL - t) / TRAIL);
          CRGB c = playerCol[p]; c.nscale8(bri);
          sp(px, py, c);
        }
      }
      FastLED.show();
      delay(20);
    }
    if (rep < REPS - 1) {
      FastLED.clear();
      drawNet();
      FastLED.show();
      delay(PAUSE_MS);
    }
  }
  update(_animScore);
}

// Break timer: M:SS in place of the score, set badges still shown.
inline void showBreakTimer(uint32_t remainingMs, bool colonOn) {
  _timerMode = true;
  FastLED.clear();
  uint32_t totalSec = (remainingMs + 999) / 1000;
  int mins = (int)(totalSec / 60);
  int secs = (int)(totalSec % 60);
  CRGB col = CRGB(0, 210, 80);  // green

  if (!_matrixLarge) {
    // Small 24×8: +1 LED gap between each element, centered at x=12
    // mins x=3..6 | gap x=7..8 | colon x=9 | gap x=10..11 | tens x=12..15 | gap x=16..17 | units x=18..21
    drawDigit(mins,      3, 0, col);
    if (colonOn) { _leds[xy(9, 2)] = col; _leds[xy(9, 5)] = col; }
    drawDigit(secs / 10, 12, 0, col);
    drawDigit(secs % 10, 18, 0, col);
    drawSetDigitSm(_animScore.setA, 5,  Config::NUM_ROWS, COLOR_A);
    drawSetDigitSm(_animScore.setB, 16, Config::NUM_ROWS, COLOR_B);
  } else {
    // Large 32×16: +1 LED gap, centered at x=16
    // mins x=3..8 | gap x=9..10 | colon x=11 | gap x=12..14 | tens x=15..20 | gap x=21..23 | units x=24..29
    drawDigitLarge(mins,      3, 0, col);
    if (colonOn) { _leds[xyLarge(11, 3)] = col; _leds[xyLarge(11, 7)] = col; }
    drawDigitLarge(secs / 10, 15, 0, col);
    drawDigitLarge(secs % 10, 24, 0, col);
    drawDigitSmall(_animScore.setA, 2,  12, COLOR_A);
    drawDigitSmall(_animScore.setB, 27, 12, COLOR_B);
  }
  FastLED.show();
}

// ─── Timeout display ─────────────────────────────────────────────────────────
// 3×5 glyphs for "TIMEOUT" (I = plain vertical bar, E has proper middle bar).
// Small matrix layout: timer rows 0..5 | gap row 6 | text rows 7..11.
// Text scrolls right-to-left (stride=4: 3px char + 1px gap → 27px total > 24px).

static const uint8_t _TG_T[5][3] = {{1,1,1},{0,1,0},{0,1,0},{0,1,0},{0,1,0}};
static const uint8_t _TG_I[5][3] = {{0,1,0},{0,1,0},{0,1,0},{0,1,0},{0,1,0}};  // plain bar
static const uint8_t _TG_M[5][3] = {{1,0,1},{1,1,1},{1,0,1},{1,0,1},{1,0,1}};
static const uint8_t _TG_E[5][3] = {{1,1,1},{1,0,0},{1,1,0},{1,0,0},{1,1,1}};
static const uint8_t _TG_O[5][3] = {{1,1,1},{1,0,1},{1,0,1},{1,0,1},{1,1,1}};
static const uint8_t _TG_U[5][3] = {{1,0,1},{1,0,1},{1,0,1},{1,0,1},{1,1,1}};

inline void showTimeoutDisplay(uint32_t remainingMs) {
  _timerMode = true;
  FastLED.clear();

  CRGB timerCol = CRGB(0, 210, 80);   // green countdown
  CRGB textCol  = CRGB(255, 120, 0);  // orange "TIME OUT"

  uint32_t totalSec = (remainingMs + 999) / 1000;
  int mins = (int)(totalSec / 60);
  int secs = (int)(totalSec % 60);

  const uint8_t (*msg[7])[3] = {_TG_T,_TG_I,_TG_M,_TG_E,_TG_O,_TG_U,_TG_T};
  // Per-char x offsets: stride=4 within TIME and within OUT,
  // 3-LED gap between E and O.
  // T=0, I=4, M=8, E=12 | gap 15+16+17 | O=18, U=22, T=26 → TEXT_W=29
  static const int8_t CHAR_X[7] = {0, 4, 8, 12, 18, 22, 26};
  static constexpr int TEXT_W = 29;  // spans x=0..28

  if (!_matrixLarge) {
    // Timer: rows 0..6 using the standard 7-row digit shapes
    drawDigit(mins,      3, 0, timerCol);
    _leds[xy(9, 2)] = timerCol; _leds[xy(9, 5)] = timerCol;
    drawDigit(secs / 10, 12, 0, timerCol);
    drawDigit(secs % 10, 18, 0, timerCol);
    // Multiple copies scroll continuously in rows 7..11, 5-LED gap between instances.
    // LOOP_W = TEXT_W + gap = 34. Copies at xOff + k*LOOP_W cover the display.
    const int COLS   = (int)Config::NUM_COLS;
    const int LOOP_W = TEXT_W + 5;  // 34
    int scroll = (int)((millis() * 10UL / 1000UL) % (uint32_t)LOOP_W);
    int xOff   = -scroll;  // base copy starts at x=0 when scroll=0
    for (int k = 0; k <= 2; k++) {
      int copyOff = xOff + k * LOOP_W;
      if (copyOff >= COLS || copyOff + TEXT_W <= 0) continue;
      for (int ci = 0; ci < 7; ci++) {
        int charX = copyOff + CHAR_X[ci];
        for (int gy = 0; gy < 5; gy++)
          for (int gx = 0; gx < 3; gx++) {
            if (!msg[ci][gy][gx]) continue;
            int px = charX + gx;
            int py = (int)Config::NUM_ROWS - 1 + gy;  // y = 7..11
            if (px >= 0 && px < COLS) _leds[xy(px, py)] = textCol;
          }
      }
    }
  } else {
    // Large matrix: full 11-row digits, text scrolls in rows 11..15 (5 rows)
    drawDigitLarge(mins,      3, 0, timerCol);
    _leds[xyLarge(11, 3)] = timerCol; _leds[xyLarge(11, 7)] = timerCol;
    drawDigitLarge(secs / 10, 15, 0, timerCol);
    drawDigitLarge(secs % 10, 24, 0, timerCol);
    const int LOOP_W = TEXT_W + 5;  // 34
    int scroll = (int)((millis() * 10UL / 1000UL) % (uint32_t)LOOP_W);
    int xOff   = -scroll;
    for (int k = 0; k <= 2; k++) {
      int copyOff = xOff + k * LOOP_W;
      if (copyOff >= L_COLS || copyOff + TEXT_W <= 0) continue;
      for (int ci = 0; ci < 7; ci++) {
        int charX = copyOff + CHAR_X[ci];
        for (int gy = 0; gy < 5; gy++)
          for (int gx = 0; gx < 3; gx++) {
            if (!msg[ci][gy][gx]) continue;
            int px = charX + gx;
            int py = 11 + gy;  // y = 11..15
            if (px >= 0 && px < L_COLS) _leds[xyLarge(px, py)] = textCol;
          }
      }
    }
  }
  FastLED.show();
}

} // namespace LED
