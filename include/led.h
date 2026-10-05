/**
 * led.h - LED display for 24×8 (+ 4-row extension) matrix
 */

#pragma once
#include <Arduino.h>
#include <FastLED.h>
#include "freeze_debug.h"
#include <Preferences.h>
#include "config.h"
#include "score.h"
#include "team_names.h"

namespace LED {

// Main panel: 24×8 = 192 LEDs. Extension panel: 24×4 = 96 LEDs. Total: 288.
static constexpr int TOTAL_LEDS = 288;

// Every draw function renders into _leds (the canvas) and hands it to
// _present(), which builds _out — what FastLED actually sends — from it,
// fading between scenes (see "Scene transitions" below).
static CRGB        _leds[TOTAL_LEDS];
static CRGB        _out[TOTAL_LEDS];
static CRGB        _last[TOTAL_LEDS];  // canvas as of the last _present()
static CRGB        _prev[TOTAL_LEDS];  // outgoing scene, frozen while it fades out
static Preferences _prefs;

static const CRGB COLOR_A = CRGB(255, 100, 0);
static const CRGB COLOR_B = CRGB::Cyan;
// Neutral text (labels, timers, separators): green rather than white/grey —
// about as readable (the eye is most sensitive to green) for roughly half the
// LED current of a grey, which lights all three channels.
static const CRGB TEXT_GREEN     = CRGB(0, 210, 80);
static const CRGB TEXT_GREEN_DIM = CRGB(0, 120, 45);  // secondary: "VS", score dash

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
        if (px >= 0 && px < Config::NUM_COLS && py >= 0 && py < (int)(Config::NUM_ROWS + 4))
          _leds[xy(px, py)] = col;
      }
}

// ─── 5×7 text font (A-Z, 0-9, space, apostrophe, hyphen) ─────────────────────
// Used by the pre-match team-name marquee (showTeamIntro). Indices:
// 0-25 = A-Z, 26-35 = 0-9, 36 = space, 37 = apostrophe, 38 = hyphen.

static constexpr int FONT5X7_SPACE = 36;
static constexpr int FONT5X7_APOS  = 37;
static constexpr int FONT5X7_DASH  = 38;

static const uint8_t FONT5X7[39][7][5] = {
  {{0,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{1,1,1,1,1},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1}}, // A
  {{1,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{1,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{1,1,1,1,0}}, // B
  {{0,1,1,1,0},{1,0,0,0,1},{1,0,0,0,0},{1,0,0,0,0},{1,0,0,0,0},{1,0,0,0,1},{0,1,1,1,0}}, // C
  {{1,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,1,1,1,0}}, // D
  {{1,1,1,1,1},{1,0,0,0,0},{1,0,0,0,0},{1,1,1,1,0},{1,0,0,0,0},{1,0,0,0,0},{1,1,1,1,1}}, // E
  {{1,1,1,1,1},{1,0,0,0,0},{1,0,0,0,0},{1,1,1,1,0},{1,0,0,0,0},{1,0,0,0,0},{1,0,0,0,0}}, // F
  {{0,1,1,1,0},{1,0,0,0,1},{1,0,0,0,0},{1,0,1,1,1},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0}}, // G
  {{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,1,1,1,1},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1}}, // H
  {{1,1,1,1,1},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{1,1,1,1,1}}, // I
  {{0,0,1,1,1},{0,0,0,1,0},{0,0,0,1,0},{0,0,0,1,0},{0,0,0,1,0},{1,0,0,1,0},{0,1,1,0,0}}, // J
  {{1,0,0,0,1},{1,0,0,1,0},{1,0,1,0,0},{1,1,0,0,0},{1,0,1,0,0},{1,0,0,1,0},{1,0,0,0,1}}, // K
  {{1,0,0,0,0},{1,0,0,0,0},{1,0,0,0,0},{1,0,0,0,0},{1,0,0,0,0},{1,0,0,0,0},{1,1,1,1,1}}, // L
  {{1,0,0,0,1},{1,1,0,1,1},{1,0,1,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1}}, // M
  {{1,0,0,0,1},{1,1,0,0,1},{1,0,1,0,1},{1,0,0,1,1},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1}}, // N
  {{0,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0}}, // O
  {{1,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{1,1,1,1,0},{1,0,0,0,0},{1,0,0,0,0},{1,0,0,0,0}}, // P
  {{0,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,0,1,0,1},{1,0,0,1,0},{0,1,1,0,1}}, // Q
  {{1,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{1,1,1,1,0},{1,0,1,0,0},{1,0,0,1,0},{1,0,0,0,1}}, // R
  {{0,1,1,1,1},{1,0,0,0,0},{1,0,0,0,0},{0,1,1,1,0},{0,0,0,0,1},{0,0,0,0,1},{1,1,1,1,0}}, // S
  {{1,1,1,1,1},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0}}, // T
  {{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0}}, // U
  {{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{0,1,0,1,0},{0,0,1,0,0}}, // V
  {{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,0,1,0,1},{1,0,1,0,1},{1,1,0,1,1},{1,0,0,0,1}}, // W
  {{1,0,0,0,1},{1,0,0,0,1},{0,1,0,1,0},{0,0,1,0,0},{0,1,0,1,0},{1,0,0,0,1},{1,0,0,0,1}}, // X
  {{1,0,0,0,1},{1,0,0,0,1},{0,1,0,1,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0}}, // Y
  {{1,1,1,1,1},{0,0,0,0,1},{0,0,0,1,0},{0,0,1,0,0},{0,1,0,0,0},{1,0,0,0,0},{1,1,1,1,1}}, // Z
  {{0,1,1,1,0},{1,0,0,0,1},{1,0,0,1,1},{1,0,1,0,1},{1,1,0,0,1},{1,0,0,0,1},{0,1,1,1,0}}, // 0
  {{0,0,1,0,0},{0,1,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,1,1,1,0}}, // 1
  {{0,1,1,1,0},{1,0,0,0,1},{0,0,0,0,1},{0,0,0,1,0},{0,0,1,0,0},{0,1,0,0,0},{1,1,1,1,1}}, // 2
  {{0,1,1,1,0},{1,0,0,0,1},{0,0,0,0,1},{0,0,1,1,0},{0,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0}}, // 3
  {{0,0,0,1,0},{0,0,1,1,0},{0,1,0,1,0},{1,0,0,1,0},{1,1,1,1,1},{0,0,0,1,0},{0,0,0,1,0}}, // 4
  {{1,1,1,1,1},{1,0,0,0,0},{1,1,1,1,0},{0,0,0,0,1},{0,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0}}, // 5
  {{0,0,1,1,0},{0,1,0,0,0},{1,0,0,0,0},{1,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0}}, // 6
  {{1,1,1,1,1},{0,0,0,0,1},{0,0,0,1,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0}}, // 7
  {{0,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0}}, // 8
  {{0,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,1},{0,0,0,0,1},{0,0,0,1,0},{0,1,1,0,0}}, // 9
  {{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0}}, // space
  {{0,1,0,0,0},{0,1,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0}}, // '
  {{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{1,1,1,1,1},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0}}, // -
};

inline int _fontCharIndex(char c) {
  c = (char)toupper((unsigned char)c);
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= '0' && c <= '9') return 26 + (c - '0');
  if (c == '\'') return FONT5X7_APOS;
  if (c == '-')  return FONT5X7_DASH;
  return FONT5X7_SPACE;
}

inline void drawChar5x7(char c, int sx, int sy, CRGB col) {
  int idx = _fontCharIndex(c);
  for (int y = 0; y < 7; y++)
    for (int x = 0; x < 5; x++)
      if (FONT5X7[idx][y][x]) {
        int px = sx + x, py = sy + y;
        if (px >= 0 && px < Config::NUM_COLS && py >= 0 && py < (int)(Config::NUM_ROWS + 4))
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

// ─── Scene transitions ───────────────────────────────────────────────────────
// Each frame is presented with a scene key — which screen it is (score,
// "timeout for team A", break...), not its content. A new key fades the whole
// old frame out, then the new one in, TRANSITION_FADE_MS each way. Within a
// scene, draw functions register digit regions (_region(), fixed slot per
// digit position): only a slot whose content changed fades, inside its own
// rectangle, while the rest of the frame (serve dots, scrolling text...) stays
// live. Same rules for both: a change arriving mid-fade-out only replaces what
// fades in next; the old content coming back before it's gone (e.g. a
// one-frame score redraw during the end-of-match summary, or a +1 undone
// straight away) fades straight back in from where it was.

static constexpr uint32_t TRANSITION_FADE_MS  = 180;
static constexpr uint16_t TIMER_DIGIT_FADE_MS = 120;  // countdowns: digit not dim for long every second

static constexpr uint32_t SCENE_BLANK   = 0;
static constexpr uint32_t SCENE_SCORE   = 1u << 28;
static constexpr uint32_t SCENE_TIMEOUT = 2u << 28;
static constexpr uint32_t SCENE_MEDICAL = 3u << 28;
static constexpr uint32_t SCENE_BREAK   = 4u << 28;
static constexpr uint32_t SCENE_SLEEP   = 5u << 28;
static constexpr uint32_t SCENE_PROMPT  = 6u << 28;
static constexpr uint32_t SCENE_INTRO   = 7u << 28;
static constexpr uint32_t SCENE_SUMMARY = 8u << 28;

// Fade level (0..255) for linear progress p (0 = dark, 1 = full). Smootherstep
// so it eases out of and into black instead of starting/stopping abruptly,
// then gamma 2.2 so the brightness change looks even to the eye — a linear
// pixel ramp seems to jump at the dark end and crawl at the bright end.
// Recomputed every frame (~60 fps) rather than stepped; FastLED's temporal
// dithering smooths the low end even at the usual low panel brightness.
inline uint8_t _fadeLevel(float p) {
  p = constrain(p, 0.0f, 1.0f);
  float e = p * p * p * (p * (p * 6.0f - 15.0f) + 10.0f);
  return (uint8_t)(powf(e, 2.2f) * 255.0f + 0.5f);
}

enum _TransPhase : uint8_t { TR_IDLE, TR_OUT, TR_IN };
static _TransPhase _trPhase    = TR_IDLE;
static uint32_t    _shownKey   = SCENE_BLANK;  // scene shown, or next to fade in
static uint32_t    _prevKey    = SCENE_BLANK;  // scene fading out (TR_OUT)
static float       _trFrom     = 1.0f;         // progress at _trStartMs
static uint32_t    _trStartMs  = 0;
static uint32_t    _lastShowMs = 0;

inline float _trProgress(uint32_t now) {
  float d = (float)(now - _trStartMs) / TRANSITION_FADE_MS;
  return _trPhase == TR_OUT ? _trFrom - d : _trFrom + d;
}

// Digit regions. Slot = fixed digit position within a scene (score: 0/1 A
// tens/units, 2/3 B tens/units, 4/5 set digits; timers: 0 minutes, 1/2
// seconds); key = what's drawn there (0 = nothing).
static constexpr int MAX_REGIONS = 6;
struct _Rect {  // inclusive; x1 < x0 = empty
  int8_t x0, y0, x1, y1;
  _Rect() : x0(0), y0(0), x1(-1), y1(-1) {}
  _Rect(int a, int b, int c, int d) : x0((int8_t)a), y0((int8_t)b), x1((int8_t)c), y1((int8_t)d) {}
};
struct _Region { uint32_t key = 0; _Rect r; uint16_t durMs = TRANSITION_FADE_MS; };
struct _RegionFade {
  uint32_t    key = 0, prevKey = 0;  // shown (or next to fade in) / fading out
  _Rect       r;                     // rect of what `key` draws
  _Rect       fade;                  // rect being faded (old ∪ new)
  _TransPhase phase = TR_IDLE;
  float       from = 1.0f;
  uint32_t    startMs = 0;
  uint16_t    durMs = TRANSITION_FADE_MS;
};
static _Region     _reqReg[MAX_REGIONS];   // registered by the frame being drawn
static _RegionFade _regFade[MAX_REGIONS];  // what each slot shows / is fading
static CRGB        _regPrev[TOTAL_LEDS];   // outgoing digits (inside fade rects only)

inline bool _rectEmpty(const _Rect& r) { return r.x1 < r.x0; }
inline bool _inRect(const _Rect& r, int x, int y) { return x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1; }
inline _Rect _union(const _Rect& a, const _Rect& b) {
  if (_rectEmpty(a)) return b;
  if (_rectEmpty(b)) return a;
  return { (int8_t)min(a.x0, b.x0), (int8_t)min(a.y0, b.y0), (int8_t)max(a.x1, b.x1), (int8_t)max(a.y1, b.y1) };
}

inline void _region(int slot, uint32_t key, int x, int y, int w, int h, uint16_t durMs) {
  _Region& r = _reqReg[slot];
  r.key   = key;
  r.r     = { (int8_t)max(x, 0), (int8_t)max(y, 0),
              (int8_t)min(x + w - 1, (int)Config::NUM_COLS - 1),
              (int8_t)min(y + h - 1, (int)Config::NUM_ROWS + 3) };
  r.durMs = durMs;
}
inline uint32_t _glyphKey(int d, int x, int y) {
  return 1u | ((uint32_t)(d & 0x7F) << 1) | ((uint32_t)(x & 0xFF) << 8) | ((uint32_t)(y & 0xFF) << 16);
}

// Starts / redirects a slot's fade when its content changed (same scene).
inline void _updateRegion(int i, uint32_t now) {
  _Region& r = _reqReg[i];
  _RegionFade& f = _regFade[i];
  if (r.key == f.key) return;
  float p = (f.phase == TR_IDLE) ? 1.0f
          : (float)(now - f.startMs) / f.durMs * (f.phase == TR_OUT ? -1.0f : 1.0f) + f.from;
  if (f.phase == TR_OUT && r.key == f.prevKey) {
    f.phase = TR_IN; f.from = max(p, 0.0f); f.startMs = now;
  } else {
    bool keepSnap = (f.phase == TR_OUT);   // old digit already captured
    _Rect old = keepSnap ? f.fade : _Rect{};
    f.fade = _union(f.phase == TR_IDLE ? f.r : f.fade, r.r);
    for (int y = f.fade.y0; y <= f.fade.y1; y++)
      for (int x = f.fade.x0; x <= f.fade.x1; x++)
        if (!keepSnap || !_inRect(old, x, y)) _regPrev[xy(x, y)] = _last[xy(x, y)];
    if (!keepSnap) {
      f.prevKey = f.key; f.phase = TR_OUT; f.from = p; f.startMs = now; f.durMs = r.durMs;
    }
  }
  f.key = r.key;
  f.r   = r.r;
}

// Advances a slot's fade; returns its level, or -1 when idle.
inline int _regionLevel(_RegionFade& f, uint32_t now) {
  if (f.phase == TR_IDLE) return -1;
  float d = (float)(now - f.startMs) / f.durMs;
  if (f.phase == TR_OUT) {
    float p = f.from - d;
    if (p > 0.0f) return _fadeLevel(p);
    f.phase = TR_IN; f.from = 0.0f; f.startMs = now;
    return 0;
  }
  float p = f.from + d;
  if (p >= 1.0f) { f.phase = TR_IDLE; return -1; }
  return _fadeLevel(p);
}

inline bool _anyFading() {
  if (_trPhase != TR_IDLE) return true;
  for (int i = 0; i < MAX_REGIONS; i++) if (_regFade[i].phase != TR_IDLE) return true;
  return false;
}

// New frame: blank canvas, no digit regions until the draw registers them.
inline void _clear() {
  fill_solid(_leds, TOTAL_LEDS, CRGB::Black);
  for (int i = 0; i < MAX_REGIONS; i++) _reqReg[i] = _Region{};
}

inline void _scaleInto(CRGB* dst, const CRGB* src, uint8_t level) {
  for (int i = 0; i < TOTAL_LEDS; i++) { dst[i] = src[i]; dst[i].nscale8(level); }
}

// Show the canvas as scene `key`, fading if the scene changed.
inline void _present(uint32_t key) {
  uint32_t now = millis();
  if (key != _shownKey) {
    float p = (_trPhase == TR_IDLE) ? 1.0f : _trProgress(now);
    if (_trPhase == TR_OUT) {
      if (key == _prevKey) { _trPhase = TR_IN; _trFrom = max(p, 0.0f); _trStartMs = now; }
    } else if (_shownKey == SCENE_BLANK) {
      _trPhase = TR_IN; _trFrom = 0.0f; _trStartMs = now;  // nothing to fade out
    } else {
      memcpy(_prev, _last, sizeof(_prev));
      _prevKey = _shownKey;
      _trPhase = TR_OUT; _trFrom = p; _trStartMs = now;
    }
    _shownKey = key;
    // The whole frame fades — digit slots just adopt the new scene's digits.
    for (int i = 0; i < MAX_REGIONS; i++) {
      _regFade[i] = _RegionFade{};
      _regFade[i].key = _reqReg[i].key;
      _regFade[i].r   = _reqReg[i].r;
    }
  } else if (_trPhase == TR_IDLE) {
    for (int i = 0; i < MAX_REGIONS; i++) _updateRegion(i, now);
  }

  float p = 1.0f;
  if (_trPhase == TR_OUT) {
    p = _trProgress(now);
    if (p <= 0.0f) { _trPhase = TR_IN; _trFrom = 0.0f; _trStartMs = now; p = 0.0f; }
  } else if (_trPhase == TR_IN) {
    p = _trProgress(now);
    if (p >= 1.0f) _trPhase = TR_IDLE;
  }

  memcpy(_last, _leds, sizeof(_last));
  if      (_trPhase == TR_OUT) _scaleInto(_out, _prev, _fadeLevel(p));
  else if (_trPhase == TR_IN)  _scaleInto(_out, _leds, _fadeLevel(p));
  else {
    memcpy(_out, _leds, sizeof(_out));
    for (int i = 0; i < MAX_REGIONS; i++) {
      _RegionFade& f = _regFade[i];
      bool out = (f.phase == TR_OUT);
      int level = _regionLevel(f, now);
      if (level < 0) continue;
      const CRGB* src = out ? _regPrev : _leds;
      for (int y = f.fade.y0; y <= f.fade.y1; y++)
        for (int x = f.fade.x0; x <= f.fade.x1; x++) {
          int idx = xy(x, y);
          _out[idx] = src[idx];
          _out[idx].nscale8((uint8_t)level);
        }
    }
  }
  _lastShowMs = now;
  FD_SHOW();
}

// Boot animation only: straight to the panel, no transition.
inline void _showRaw() {
  memcpy(_out, _leds, sizeof(_out));
  FD_SHOW();
}

// ─── Breathing animation state ───────────────────────────────────────────────

static Score    _animScore;
static uint32_t _lastTick         = 0;
static bool     _timerMode        = false;

// Rotation ring is shown whenever the score sits at a rotation point — derived
// live from state, just like the serve indicator, so it reflects corrections
// (decrements) too, not just the increment that first reached the threshold.
inline bool _isRotationPoint(const Score& score) {
  return !isSetWon(score) && ((score.scoreA + score.scoreB) % 4) == 3;
}

inline uint8_t _breatheFactor() {
  return sin8((uint8_t)(millis() / 5));
}

// ─── Serve pixel helper ───────────────────────────────────────────────────────

// Two dots (COLOR_A and COLOR_B), phase-opposite, stepping CCW through the 8
// explicit ring pixels — no trig rounding means corners are never touched.
inline void _drawRotationLoop() {
  // 8 valid ring pixels in CCW order (no corners)
  static const int8_t RING[8][2] = {
    {13, 9}, {12, 8}, {11, 8}, {10, 9},
    {10,10}, {11,11}, {12,11}, {13,10}
  };
  static constexpr uint32_t PERIOD = 1600;  // ms per full revolution
  static constexpr int      TRAIL  = 3;

  int headIdx = (int)((float)(millis() % PERIOD) / PERIOD * 8.0f) & 7;

  // Clear ring area
  for (int x = 9; x <= 14; x++)
    for (int y = 8; y <= 11; y++)
      _leds[xy(x, y)] = CRGB::Black;

  // Two dots 4 positions (180°) apart, each with a fading trail
  for (int d = 0; d < 2; d++) {
    CRGB col  = (d == 0) ? COLOR_A : COLOR_B;
    int  base = headIdx + d * 4;
    for (int t = TRAIL; t >= 0; t--) {  // oldest first so head overwrites
      int     idx = (base - t + 32) & 7;
      // Exponential falloff: each trail step halves brightness (230 → 60 → 30 → 15)
      uint8_t bri = (t == 0) ? 230 : (uint8_t)(120u >> t);
      CRGB    c   = col; c.nscale8(bri);
      _leds[xy(RING[idx][0], RING[idx][1])] = c;
    }
  }
}

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

// Big digit that fades on its own when it changes: slot < 0 = no fade region.
inline void _fadeDigit(int slot, int d, int x, int y, CRGB col, uint16_t durMs) {
  drawDigit(d, x, y, col);
  if (slot >= 0) _region(slot, _glyphKey(d, x, y), x, y, 4, 7, durMs);
}

// Big A and B scores, digits from row y down (7 rows). slotBase >= 0: each
// digit gets its own fade slot (A tens, A units, B tens, B units).
inline void _drawScorePair(uint8_t scoreA, uint8_t scoreB, int y, int slotBase = -1) {
  auto slot = [&](int i) { return slotBase < 0 ? -1 : slotBase + i; };
  // Scores < 10: single digit centered in the two-digit zone (x=2..10 for A, x=13..21 for B).
  // Scores >= 10: two digits as usual.
  // Digit "1" visual center is 0.5px right; shift left by 1 to compensate.
  if (scoreA < 10) {
    _fadeDigit(slot(1), scoreA, 5, y, COLOR_A, TRANSITION_FADE_MS);
  } else {
    int dAs_t = scoreA / 10, dAs_u = scoreA % 10;
    _fadeDigit(slot(0), dAs_t, 2 + (dAs_t == 1 ? -1 : 0), y, COLOR_A, TRANSITION_FADE_MS);
    _fadeDigit(slot(1), dAs_u, 7 + (dAs_u == 1 ? -1 : 0), y, COLOR_A, TRANSITION_FADE_MS);
  }
  if (scoreB < 10) {
    _fadeDigit(slot(3), scoreB, 15 + (scoreB == 1 ? -1 : 0), y, COLOR_B, TRANSITION_FADE_MS);
  } else {
    _fadeDigit(slot(2), scoreB / 10, 13, y, COLOR_B, TRANSITION_FADE_MS);
    _fadeDigit(slot(3), scoreB % 10, 18, y, COLOR_B, TRANSITION_FADE_MS);
  }
}

// M:SS countdown on rows 0..6, each digit in its own fade slot (0 minutes,
// 1/2 seconds) — shared by the break, timeout and medical screens.
inline void _drawClock(uint32_t remainingMs, CRGB col, bool colonOn = true) {
  uint32_t totalSec = (remainingMs + 999) / 1000;
  int mins = (int)(totalSec / 60);
  int secs = (int)(totalSec % 60);
  _fadeDigit(0, mins,      3,  0, col, TIMER_DIGIT_FADE_MS);
  if (colonOn) { _leds[xy(9, 2)] = col; _leds[xy(9, 5)] = col; }
  _fadeDigit(1, secs / 10, 12, 0, col, TIMER_DIGIT_FADE_MS);
  _fadeDigit(2, secs % 10, 18, 0, col, TIMER_DIGIT_FADE_MS);
}

inline void _updateSmall(const Score& score, uint8_t breathe, bool fadeSlots = false) {
  _drawScorePair(score.scoreA, score.scoreB, 0, fadeSlots ? 0 : -1);

  // Set scores: 3×4 digit glyphs in extension rows (y=8..11)
  drawSetDigitSm(score.setA, 5,  Config::NUM_ROWS, COLOR_A);
  drawSetDigitSm(score.setB, 16, Config::NUM_ROWS, COLOR_B);
  if (fadeSlots) {
    _region(4, _glyphKey(score.setA, 5,  Config::NUM_ROWS), 5,  Config::NUM_ROWS, 3, 4, TRANSITION_FADE_MS);
    _region(5, _glyphKey(score.setB, 16, Config::NUM_ROWS), 16, Config::NUM_ROWS, 3, 4, TRANSITION_FADE_MS);
  }

  _applyServeSmall(score, breathe);
  if (_isRotationPoint(score)) _drawRotationLoop();
}

// ─── Public API ──────────────────────────────────────────────────────────────

inline void init() {
  FastLED.addLeds<WS2812B, Config::PIN, GRB>(_out, TOTAL_LEDS);

  _prefs.begin("led", false);
  uint8_t brightness = _prefs.getUChar("brightness", Config::BRIGHTNESS);
  _prefs.end();

  FastLED.setBrightness(brightness);
  FastLED.clear();
  FD_SHOW();
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
  _clear();
  _leds[xy((int)roundf(cx), (int)roundf(cy))] = CRGB::White;
  _showRaw();
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
    _showRaw();
    delay(22);
  }

  for (int step = 0; step < 20; step++) {
    for (int i = 0; i < TOTAL_LEDS; i++) _leds[i].nscale8(195);
    _showRaw();
    delay(18);
  }

  _clear();
  _showRaw();
}

inline void setBrightness(uint8_t brightness) {
  brightness = constrain(brightness, 1, 255);
  FastLED.setBrightness(brightness);
  FD_SHOW();
  _prefs.begin("led", false);
  _prefs.putUChar("brightness", brightness);
  _prefs.end();
  Serial.printf("[LED] Brightness: %d\n", brightness);
}

inline uint8_t getBrightness() { return FastLED.getBrightness(); }

inline void setRawBrightness(uint8_t brightness) {
  FastLED.setBrightness(constrain(brightness, 1, 255));
  FD_SHOW();
}

// Set when update() is called off Core 1 — see update().
static volatile bool _redrawPending = false;

// Every LED function here must only ever run on Core 1 (the loop task).
// FastLED.show() is not safe to call from both cores at once: two
// overlapping transmissions leave one of them waiting forever for an RMT
// "done" signal — the tournament "frozen LEDs" bug (reproduced + confirmed
// 2026-09-28 with freeze_debug.h: 9/9 freezes had both cores inside show(),
// or Core 0 stuck in show() holding scoreMutex while loop() waited on it).
// update() is the only one with Core 0 callers — ScoreActions::apply() /
// applyFromDatabase(), reached from firebaseTask and ESP-NOW callbacks — so
// off Core 1 it just flags a redraw, which loop() performs a few ms later
// via takePendingRedraw(). Those callers always pass currentScore, which is
// exactly what loop() redraws from.
inline void update(const Score& score) {
  if (xPortGetCoreID() != ARDUINO_RUNNING_CORE) {
    _redrawPending = true;
    return;
  }
  _redrawPending = false;
  _animScore = score;
  _timerMode = false;
  _clear();
  _updateSmall(score, _breatheFactor(), true);
  _present(SCENE_SCORE);
}

// loop() only: true once if an off-core update() asked for a redraw.
inline bool takePendingRedraw() {
  if (!_redrawPending) return false;
  _redrawPending = false;
  return true;
}

// Call from main loop: animates serve pixels at ~30 fps, and keeps a running
// scene transition going at ~60 fps whatever the scene (the break timer, for
// one, only redraws every 250ms).
inline void tick() {
  uint32_t now = millis();
  if (_anyFading() && now - _lastShowMs >= 16) _present(_shownKey);
  if (now - _lastTick < 33) return;
  _lastTick = now;
  if (_timerMode) return;
  _applyServeSmall(_animScore, _breatheFactor());
  if (_isRotationPoint(_animScore)) _drawRotationLoop();
  _present(SCENE_SCORE);
}

// Sleep animation: 4 LEDs centred (cols 10-13, row 5), yellow→cyan travelling wave
inline void showSleepAnimation() {
  _clear();
  uint8_t phase = (uint8_t)(millis() / 10);  // 256 × 10 ms = 2.56 s cycle
  static const CRGB palette[4] = {
    CRGB(255, 255,   0),  // yellow
    CRGB(170, 255,  85),  // lime
    CRGB( 85, 255, 170),  // spring
    CRGB(  0, 255, 255),  // cyan
  };
  for (int i = 0; i < 4; i++) {
    uint8_t b = sin8(phase - (uint8_t)(i * 64));
    CRGB col = palette[i];
    col.nscale8(b);
    _leds[xy(10 + i, 5)] = col;
  }
  _present(SCENE_SLEEP);
}

// Break timer: M:SS with set badges
inline void showBreakTimer(uint32_t remainingMs, bool colonOn) {
  _timerMode = true;
  _clear();
  _drawClock(remainingMs, TEXT_GREEN, colonOn);
  drawSetDigitSm(_animScore.setA, 5,  Config::NUM_ROWS, COLOR_A);
  drawSetDigitSm(_animScore.setB, 16, Config::NUM_ROWS, COLOR_B);
  _present(SCENE_BREAK);
}

// ─── Timeout display ─────────────────────────────────────────────────────────

static const uint8_t _TG_T[5][3] = {{1,1,1},{0,1,0},{0,1,0},{0,1,0},{0,1,0}};
static const uint8_t _TG_I[5][3] = {{1,1,1},{0,1,0},{0,1,0},{0,1,0},{1,1,1}};
static const uint8_t _TG_M[5][3] = {{1,0,1},{1,1,1},{1,0,1},{1,0,1},{1,0,1}};
static const uint8_t _TG_E[5][3] = {{1,1,1},{1,0,0},{1,1,0},{1,0,0},{1,1,1}};
static const uint8_t _TG_O[5][3] = {{1,1,1},{1,0,1},{1,0,1},{1,0,1},{1,1,1}};
static const uint8_t _TG_U[5][3] = {{1,0,1},{1,0,1},{1,0,1},{1,0,1},{1,1,1}};

// Countdown digits in the calling team's own color (0 = A, 1 = B — see
// ScoreActions::activeTimerTeam()), the usual green when no team called it.
inline CRGB _timerTeamColor(int8_t team) {
  if (team == 0) return COLOR_A;
  if (team == 1) return COLOR_B;
  return TEXT_GREEN;
}

inline void showTimeoutDisplay(uint32_t remainingMs, int8_t team = -1) {
  _timerMode = true;
  _clear();

  CRGB timerCol = _timerTeamColor(team);
  // Green, not the old orange - that was nearly identical to team A's
  // COLOR_A, which now also colors the countdown when team A calls it.
  CRGB textCol  = TEXT_GREEN;

  const uint8_t (*msg[7])[3] = {_TG_T,_TG_I,_TG_M,_TG_E,_TG_O,_TG_U,_TG_T};
  static const int8_t CHAR_X[7] = {0, 4, 8, 12, 18, 22, 26};
  static constexpr int TEXT_W = 29;

  _drawClock(remainingMs, timerCol);

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
  _present(SCENE_TIMEOUT | (uint32_t)(team + 1));
}

// ─── Medical timer display ────────────────────────────────────────────────────

static const uint8_t _TG_D[5][3] = {{1,1,0},{1,0,1},{1,0,1},{1,0,1},{1,1,0}};
static const uint8_t _TG_C[5][3] = {{0,1,1},{1,0,0},{1,0,0},{1,0,0},{0,1,1}};
static const uint8_t _TG_A[5][3] = {{0,1,0},{1,0,1},{1,1,1},{1,0,1},{1,0,1}};
static const uint8_t _TG_L[5][3] = {{1,0,0},{1,0,0},{1,0,0},{1,0,0},{1,1,1}};

inline void showMedicalTimer(uint32_t remainingMs, int8_t team = -1) {
  _timerMode = true;
  _clear();

  CRGB timerCol = _timerTeamColor(team);
  CRGB textCol  = CRGB(255, 80, 80);

  const uint8_t (*msg[7])[3] = {_TG_M,_TG_E,_TG_D,_TG_I,_TG_C,_TG_A,_TG_L};
  static const int8_t CHAR_X[7] = {0, 4, 8, 12, 16, 20, 24};
  static constexpr int TEXT_W = 27;

  _drawClock(remainingMs, timerCol);

  const int COLS   = (int)Config::NUM_COLS;
  const int LOOP_W = TEXT_W + 5;
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
  _present(SCENE_MEDICAL | (uint32_t)(team + 1));
}

// ─── Pre-match "SERVE?" prompt ───────────────────────────────────────────────
// Shown after a pedal press on the team-name marquee, until a side is picked
// (see ScoreActions::_serverSelectActive). "SERVE?" on top, then an A (team A
// colour) and B (team B colour) breathing in opposite phase underneath, each
// on its own team's side of the panel.

static const uint8_t _TG_S[5][3] = {{1,1,1},{1,0,0},{1,1,1},{0,0,1},{1,1,1}};
static const uint8_t _TG_R[5][3] = {{1,1,0},{1,0,1},{1,1,0},{1,0,1},{1,0,1}};
static const uint8_t _TG_V[5][3] = {{1,0,1},{1,0,1},{1,0,1},{1,0,1},{0,1,0}};
static const uint8_t _TG_Q[5][3] = {{1,1,1},{0,0,1},{0,1,1},{0,0,0},{0,1,0}};
static const uint8_t _TG_B[5][3] = {{1,1,0},{1,0,1},{1,1,0},{1,0,1},{1,1,0}};

inline void _drawGlyph3x5(const uint8_t (*g)[3], int sx, int sy, CRGB col) {
  for (int gy = 0; gy < 5; gy++)
    for (int gx = 0; gx < 3; gx++)
      if (g[gy][gx]) {
        int px = sx + gx, py = sy + gy;
        if (px >= 0 && px < (int)Config::NUM_COLS && py >= 0 && py < (int)(Config::NUM_ROWS + 4))
          _leds[xy(px, py)] = col;
      }
}

inline void showServerPrompt() {
  _timerMode = true;
  _clear();

  const uint8_t (*msg[6])[3] = {_TG_S,_TG_E,_TG_R,_TG_V,_TG_E,_TG_Q};
  for (int i = 0; i < 6; i++) _drawGlyph3x5(msg[i], 1 + i * 4, 1, TEXT_GREEN);

  uint8_t b = _breatheFactor();
  CRGB colA = COLOR_A; colA.nscale8(qadd8(b, 40));
  CRGB colB = COLOR_B; colB.nscale8(qadd8(255 - b, 40));
  _drawGlyph3x5(_TG_A, 5,  (int)Config::NUM_ROWS - 1, colA);
  _drawGlyph3x5(_TG_B, 16, (int)Config::NUM_ROWS - 1, colB);

  _present(SCENE_PROMPT);
}

// ─── Pre-match team-name marquee ─────────────────────────────────────────────
// Full-panel scrolling takeover shown whenever the match hasn't started yet
// (score 0-0, no sets played) and at least one team name is configured.
// Reuses _timerMode so LED::tick()'s serve-dot animation stays suppressed,
// same as showBreakTimer/showTimeoutDisplay.

inline void showTeamIntro(const TeamNames::Names& n) {
  _timerMode = true;
  _clear();

  const char* left  = n.teamA[0] ? n.teamA : "TEAM A";
  const char* right = n.teamB[0] ? n.teamB : "TEAM B";
  static const char* SEP = " VS ";

  int leftLen  = (int)strlen(left);
  int sepLen   = (int)strlen(SEP);
  int rightLen = (int)strlen(right);

  static constexpr int CHAR_W = 6;  // 5px glyph + 1px gap
  static constexpr uint32_t SPEED_PX_PER_SEC = 20;

  // One loop = "A VS B VS " — the trailing separator joins B back to the
  // next repeat of A, so "VS" shows between the names in both directions.
  int textW = (leftLen + sepLen + rightLen + sepLen) * CHAR_W;
  int loopW = textW;

  const int COLS = (int)Config::NUM_COLS;
  const int Y    = 2; // vertically centered in the 12-row extended canvas (rows 2..8)

  int scroll = (int)((millis() * SPEED_PX_PER_SEC / 1000UL) % (uint32_t)loopW);

  for (int k = 0; k <= 2; k++) {
    int xOff = -scroll + k * loopW;
    if (xOff >= COLS || xOff + textW <= 0) continue;

    int cx = xOff;
    for (int i = 0; i < leftLen;  i++) { drawChar5x7(left[i],  cx, Y, COLOR_A);   cx += CHAR_W; }
    for (int i = 0; i < sepLen;   i++) { drawChar5x7(SEP[i],   cx, Y, TEXT_GREEN_DIM); cx += CHAR_W; }
    for (int i = 0; i < rightLen; i++) { drawChar5x7(right[i], cx, Y, COLOR_B);   cx += CHAR_W; }
    for (int i = 0; i < sepLen;   i++) { drawChar5x7(SEP[i],   cx, Y, TEXT_GREEN_DIM); cx += CHAR_W; }
  }

  // Keyed by the names, so new names mid-marquee fade over too.
  uint32_t h = 5381;
  for (const char* c = left;  *c; c++) h = h * 33 + (uint8_t)*c;
  for (const char* c = right; *c; c++) h = h * 33 + (uint8_t)*c;
  _present(SCENE_INTRO | (h & 0x0FFFFFFF));
}

// ─── End-of-match summary ────────────────────────────────────────────────────
// Shown once isMatchOver() (score.h): for each finished set "SET n" and that
// set's score, each faded in, held, faded out, looping forever (the scene
// transition fades the live score out first). Purely a function of elapsedMs (time since the summary
// started) and the score, so main.cpp just calls it every frame — and a score
// correction during the summary shows up in it on the next frame.

static constexpr uint32_t SUMMARY_FADE_MS   = TRANSITION_FADE_MS;
static constexpr uint32_t SUMMARY_HOLD_MS   = 1500;
static constexpr uint32_t SUMMARY_SCREEN_MS = SUMMARY_FADE_MS + SUMMARY_HOLD_MS + SUMMARY_FADE_MS;

inline void showMatchSummary(const Score& s, uint32_t elapsedMs) {
  _timerMode = true;
  _clear();

  uint8_t level = 255;
  uint8_t sets = finishedSetCount(s);
  if (sets == 0) {
    _updateSmall(s, _breatheFactor());  // can't happen while isMatchOver()
  } else {
    uint32_t t      = elapsedMs;
    uint32_t screen = (t / SUMMARY_SCREEN_MS) % (2u * sets);
    uint32_t in     = t % SUMMARY_SCREEN_MS;
    float p = (in < SUMMARY_FADE_MS)                   ? (float)in / SUMMARY_FADE_MS
            : (in < SUMMARY_FADE_MS + SUMMARY_HOLD_MS) ? 1.0f
            : (float)(SUMMARY_SCREEN_MS - in) / SUMMARY_FADE_MS;
    level = _fadeLevel(p);

    uint8_t set = (uint8_t)(screen / 2);
    const int Y = 2;  // vertically centered in the 12-row canvas (rows 2..8)
    if (screen % 2 == 0) {
      // "SET" + set number, 5×7 font: x=0..16, number at 19..23 (full 24 cols).
      drawChar5x7('S', 0,  Y, TEXT_GREEN);
      drawChar5x7('E', 6,  Y, TEXT_GREEN);
      drawChar5x7('T', 12, Y, TEXT_GREEN);
      drawChar5x7((char)('1' + set), 19, Y, TEXT_GREEN);
    } else {
      bool live = set >= s.setA + s.setB;  // won but not yet nexted
      uint8_t a = live ? s.scoreA : s.histA[set];
      uint8_t b = live ? s.scoreB : s.histB[set];
      _drawScorePair(a, b, Y);
      _leds[xy(11, Y + 3)] = TEXT_GREEN_DIM;
      _leds[xy(12, Y + 3)] = TEXT_GREEN_DIM;
    }
  }

  if (level < 255)
    for (int i = 0; i < TOTAL_LEDS; i++) _leds[i].nscale8(level);
  _present(SCENE_SUMMARY);
}

} // namespace LED
