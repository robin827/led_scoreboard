/**
 * led.h - LED display for 24×8 (+ 4-row extension) matrix
 */

#pragma once
#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include "config.h"
#include "score.h"

namespace LED {

// Main panel: 24×8 = 192 LEDs. Extension panel: 24×4 = 96 LEDs. Total: 288.
static constexpr int TOTAL_LEDS = 288;

static CRGB        _leds[TOTAL_LEDS];
static Preferences _prefs;

static const CRGB COLOR_A = CRGB(255, 100, 0);
static const CRGB COLOR_B = CRGB::Cyan;

// ─── Coordinate mapping (24×12 extended) ─────────────────────────────────────
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
  // Extension: even cols top→bottom, odd cols bottom→top.
  int r = y - Config::NUM_ROWS;       // 0..3 within extension
  int led = (x % 2 == 0) ? x * 4 + r : x * 4 + (3 - r);
  return 192 + led;
}

// ─── Digit shapes ─────────────────────────────────────────────────────────────

static const uint8_t DIGITS[10][7][4] = {
  {{0,1,1,0},{1,0,0,1},{1,0,0,1},{1,0,0,1},{1,0,0,1},{1,0,0,1},{0,1,1,0}}, // 0
  {{0,0,1,0},{0,1,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,1,1,1}}, // 1
  {{0,1,1,0},{1,0,0,1},{0,0,0,1},{0,0,1,0},{0,1,0,0},{1,0,0,0},{1,1,1,1}}, // 2
  {{0,1,1,0},{1,0,0,1},{0,0,0,1},{0,1,1,0},{0,0,0,1},{1,0,0,1},{0,1,1,0}}, // 3
  {{0,0,1,0},{0,1,1,0},{1,0,1,0},{1,0,1,0},{1,1,1,1},{0,0,1,0},{0,0,1,0}}, // 4
  {{1,1,1,1},{1,0,0,0},{1,0,0,0},{1,1,1,0},{0,0,0,1},{1,0,0,1},{0,1,1,0}}, // 5
  {{0,1,1,0},{1,0,0,0},{1,0,0,0},{1,1,1,0},{1,0,0,1},{1,0,0,1},{0,1,1,0}}, // 6
  {{1,1,1,1},{0,0,0,1},{0,0,0,1},{0,0,1,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}}, // 7
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

// 3×4 small digit shapes for set scores (extension rows)
static const int8_t _SD0[][2] = {{0,0},{1,0},{2,0},{0,1},{2,1},{0,2},{2,2},{0,3},{1,3},{2,3}};
static const int8_t _SD1[][2] = {{1,0},{0,1},{1,1},{1,2},{0,3},{1,3},{2,3}};
static const int8_t _SD2[][2] = {{0,0},{1,0},{2,0},{2,1},{1,2},{0,3},{1,3},{2,3}};

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

// ─── Breathing animation state ───────────────────────────────────────────────

static Score    _animScore;
static uint32_t _lastTick  = 0;
static bool     _timerMode = false;

inline uint8_t _breatheFactor() {
  return sin8((uint8_t)(millis() / 5));
}

// ─── Serve pixel helper ───────────────────────────────────────────────────────

inline void _applyServeSmall(const Score& score, uint8_t breathe) {
  if (isSetWon(score)) return;
  ServeInfo srv = getServeInfo(score);
  int sc = srv.teamAServing ? 0 : 23;
  CRGB base = srv.teamAServing ? COLOR_A : COLOR_B;
  CRGB breathing = base; breathing.nscale8(breathe);
  if (srv.serveTotal == 2 && srv.servesLeft == 2) {
    _leds[xy(sc, 0)] = breathing; _leds[xy(sc, 1)] = breathing; _leds[xy(sc, 2)] = breathing;
    _leds[xy(sc, 5)] = base;      _leds[xy(sc, 6)] = base;      _leds[xy(sc, 7)] = base;
  } else if (srv.serveTotal == 2 && srv.servesLeft == 1) {
    _leds[xy(sc, 0)] = base;      _leds[xy(sc, 1)] = base;      _leds[xy(sc, 2)] = base;
    _leds[xy(sc, 5)] = breathing; _leds[xy(sc, 6)] = breathing; _leds[xy(sc, 7)] = breathing;
  } else {
    _leds[xy(sc, 5)] = breathing; _leds[xy(sc, 6)] = breathing; _leds[xy(sc, 7)] = breathing;
  }
}

// ─── Score render ─────────────────────────────────────────────────────────────

inline void _updateSmall(const Score& score, uint8_t breathe) {
  // Digit "1" visual center is 0.5px right; shift Team A left by 1 to keep display symmetric
  int dAs_t = score.scoreA / 10, dAs_u = score.scoreA % 10;
  drawDigit(dAs_t, 2 + (dAs_t == 1 ? -1 : 0), 0, COLOR_A);
  drawDigit(dAs_u, 7 + (dAs_u == 1 ? -1 : 0), 0, COLOR_A);
  drawDigit(score.scoreB / 10, 13, 0, COLOR_B);
  drawDigit(score.scoreB % 10, 18, 0, COLOR_B);

  // Set scores: 3×4 digit glyphs in extension rows (y=8..11)
  drawSetDigitSm(score.setA, 5,  Config::NUM_ROWS, COLOR_A);
  drawSetDigitSm(score.setB, 16, Config::NUM_ROWS, COLOR_B);

  _applyServeSmall(score, breathe);
}

// ─── Public API ──────────────────────────────────────────────────────────────

inline void init() {
  FastLED.addLeds<WS2812B, Config::PIN, GRB>(_leds, TOTAL_LEDS);

  _prefs.begin("led", false);
  uint8_t brightness = _prefs.getUChar("brightness", Config::BRIGHTNESS);
  _prefs.end();

  FastLED.setBrightness(brightness);
  FastLED.clear();
  FastLED.show();
  Serial.printf("[LED] Init: 24x8, brightness=%d\n", brightness);
}

inline void bootAnimation() {
  const int   cols = (int)Config::NUM_COLS;
  const int   rows = (int)(Config::NUM_ROWS + 4);  // full 24×12 incl. extension
  const float cx   = (cols - 1) / 2.0f;
  const float cy   = (rows - 1) / 2.0f;
  const float maxD = sqrtf(cx * cx + cy * cy);
  const float PI_F = 3.14159265f;

  // Brief white spark at center before explosion
  FastLED.clear();
  _leds[xy((int)roundf(cx), (int)roundf(cy))] = CRGB::White;
  FastLED.show();
  delay(80);

  const int   STEPS     = 35;
  const float beamWidth = 2.0f;

  for (int step = 0; step < STEPS + 20; step++) {
    float radius = ((float)step / STEPS) * (maxD + beamWidth);
    for (int x = 0; x < cols; x++) {
      for (int y = 0; y < rows; y++) {
        float dx   = x - cx;
        float dy   = y - cy;
        float dist = sqrtf(dx * dx + dy * dy);
        float diff = radius - dist;
        int   idx  = xy(x, y);
        if (diff < 0.0f) {
          _leds[idx] = CRGB::Black;
        } else if (diff < beamWidth) {
          uint8_t bri = (uint8_t)((1.0f - diff / beamWidth) * 255.0f);
          uint8_t hue = (uint8_t)((atan2f(dy, dx) + PI_F) / (2.0f * PI_F) * 255.0f);
          _leds[idx]  = CHSV(hue, 220, bri);
        } else {
          _leds[idx].nscale8(200);
        }
      }
    }
    FastLED.show();
    delay(22);
  }

  for (int step = 0; step < 20; step++) {
    for (int i = 0; i < TOTAL_LEDS; i++) _leds[i].nscale8(195);
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
  _updateSmall(score, _breatheFactor());
  FastLED.show();
}

// Call from main loop at ~30 fps to animate serve pixels
inline void tick() {
  uint32_t now = millis();
  if (now - _lastTick < 33) return;
  _lastTick = now;
  if (_timerMode) return;
  _applyServeSmall(_animScore, _breatheFactor());
  FastLED.show();
}

// 4 players (2 per team) rotating CCW by 90° over 2 seconds, 3 reps
inline void rotationAnimation() {
  const int   cols = (int)Config::NUM_COLS;
  const int   rows = (int)(Config::NUM_ROWS + 4);
  const float cx   = (cols - 1) * 0.5f;
  const float cy   = (rows - 1) * 0.5f;
  const float r    = 5.5f;
  static constexpr float PI2 = 6.28318530f;

  auto sp = [&](int x, int y, CRGB c) {
    if (x < 0 || x >= cols || y < 0 || y >= rows) return;
    _leds[xy(x, y)] = c;
  };

  const float startAngle[4] = { 0.0f, PI2*0.25f, PI2*0.5f, PI2*0.75f };
  const CRGB  playerCol[4]  = { COLOR_A, COLOR_A, COLOR_B, COLOR_B };
  const float netR      = 1.5f;
  const int   NET_STEPS = 12;
  const int   FRAMES    = 25;
  const int   REPS      = 3;
  const int   PAUSE_MS  = 120;
  const int   TRAIL     = 4;
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
      float rot = PI2 * 0.25f * f / (FRAMES - 1);
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

// Sleep animation: 4 LEDs centred (cols 10-13, row 5), yellow→cyan travelling wave
inline void showSleepAnimation(uint8_t brightness) {
  FastLED.clear();
  uint8_t phase = (uint8_t)(millis() / 10);  // 256 × 10 ms = 2.56 s cycle
  static const CRGB palette[4] = {
    CRGB(255, 255,   0),  // yellow
    CRGB(170, 255,  85),  // lime
    CRGB( 85, 255, 170),  // spring
    CRGB(  0, 255, 255),  // cyan
  };
  for (int i = 0; i < 4; i++) {
    uint8_t b = scale8(sin8(phase - (uint8_t)(i * 64)), brightness);
    CRGB col = palette[i];
    col.nscale8(b);
    _leds[xy(10 + i, 5)] = col;
  }
  FastLED.show();
}

// Break timer: M:SS with set badges
inline void showBreakTimer(uint32_t remainingMs, bool colonOn) {
  _timerMode = true;
  FastLED.clear();
  uint32_t totalSec = (remainingMs + 999) / 1000;
  int mins = (int)(totalSec / 60);
  int secs = (int)(totalSec % 60);
  CRGB col = CRGB(0, 210, 80);

  drawDigit(mins,      3, 0, col);
  if (colonOn) { _leds[xy(9, 2)] = col; _leds[xy(9, 5)] = col; }
  drawDigit(secs / 10, 12, 0, col);
  drawDigit(secs % 10, 18, 0, col);
  drawSetDigitSm(_animScore.setA, 5,  Config::NUM_ROWS, COLOR_A);
  drawSetDigitSm(_animScore.setB, 16, Config::NUM_ROWS, COLOR_B);
  FastLED.show();
}

// ─── Timeout display ─────────────────────────────────────────────────────────

static const uint8_t _TG_T[5][3] = {{1,1,1},{0,1,0},{0,1,0},{0,1,0},{0,1,0}};
static const uint8_t _TG_I[5][3] = {{0,1,0},{0,1,0},{0,1,0},{0,1,0},{0,1,0}};
static const uint8_t _TG_M[5][3] = {{1,0,1},{1,1,1},{1,0,1},{1,0,1},{1,0,1}};
static const uint8_t _TG_E[5][3] = {{1,1,1},{1,0,0},{1,1,0},{1,0,0},{1,1,1}};
static const uint8_t _TG_O[5][3] = {{1,1,1},{1,0,1},{1,0,1},{1,0,1},{1,1,1}};
static const uint8_t _TG_U[5][3] = {{1,0,1},{1,0,1},{1,0,1},{1,0,1},{1,1,1}};

inline void showTimeoutDisplay(uint32_t remainingMs) {
  _timerMode = true;
  FastLED.clear();

  CRGB timerCol = CRGB(0, 210, 80);
  CRGB textCol  = CRGB(255, 120, 0);

  uint32_t totalSec = (remainingMs + 999) / 1000;
  int mins = (int)(totalSec / 60);
  int secs = (int)(totalSec % 60);

  const uint8_t (*msg[7])[3] = {_TG_T,_TG_I,_TG_M,_TG_E,_TG_O,_TG_U,_TG_T};
  static const int8_t CHAR_X[7] = {0, 4, 8, 12, 18, 22, 26};
  static constexpr int TEXT_W = 29;

  drawDigit(mins,      3, 0, timerCol);
  _leds[xy(9, 2)] = timerCol; _leds[xy(9, 5)] = timerCol;
  drawDigit(secs / 10, 12, 0, timerCol);
  drawDigit(secs % 10, 18, 0, timerCol);

  const int COLS   = (int)Config::NUM_COLS;
  const int LOOP_W = TEXT_W + 5;  // 34
  int scroll = (int)((millis() * 10UL / 1000UL) % (uint32_t)LOOP_W);
  int xOff   = -scroll;
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
  FastLED.show();
}

} // namespace LED
