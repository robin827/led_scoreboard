/**
 * freeze_debug.h - Freeze detector: breadcrumbs + watcher task
 *
 * Diagnoses the "LEDs frozen, serve indicator stops blinking" bug seen in
 * FIREBASE mode at tournaments. Every task that can touch the display or
 * scoreMutex records where it currently is (FD_MARK / SCORE_LOCK / FD_SHOW —
 * each just a few pointer stores, cheap enough to leave on permanently).
 * A small high-priority watcher task, which never touches scoreMutex or the
 * LEDs itself, checks that loop() (Core 1) keeps making progress. If loop()
 * stalls for STUCK_MS, it builds a report — where loop/firebaseTask/any
 * other task last were, for how long, their FreeRTOS state, and who holds
 * scoreMutex — prints it, and reboots.
 *
 * The report is kept in RTC memory across that reboot (survives a software
 * reset, not a power cycle), then saved to NVS on the next boot, so it can
 * still be read after the fact without a serial cable: GET /freeze on the
 * portal. A reboot restores the score from NVS / Firebase, so recovering on
 * its own beats staying frozen for the rest of a match.
 */

#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

extern SemaphoreHandle_t scoreMutex;
extern TaskHandle_t firebaseTaskHandle;

namespace FreezeDebug {

enum Slot : uint8_t { LOOP = 0, FIREBASE = 1, OTHER = 2, SLOT_COUNT = 3 };
static const char* const SLOT_NAMES[SLOT_COUNT] = {"loop", "firebase", "other"};

// loop() normally comes back every ~10ms. Its only legitimately long calls
// (portal update check, WS connect) are wrapped in Pause, so a stall this
// long is a real freeze.
static constexpr uint32_t STUCK_MS = 10000;

struct Crumb {
  const char* volatile what;
  const char* volatile file;
  const char* volatile func;
  volatile uint16_t    line;
  volatile uint32_t    since;
};

static Crumb                 _crumbs[SLOT_COUNT] = {};
static TaskHandle_t volatile _tasks[2]           = {};  // LOOP, FIREBASE
static const char* volatile  _otherTaskName      = nullptr;
static volatile uint32_t     _loopBeat           = 0;
static volatile uint8_t      _pauseDepth         = 0;

static RTC_NOINIT_ATTR uint32_t _rtcMagic;
static RTC_NOINIT_ATTR char     _rtcReport[1024];
static constexpr uint32_t       RTC_MAGIC = 0xF2EE2E01;

static char     _lastReport[1024] = {};  // most recent report, loaded from NVS
static uint32_t _freezeCount      = 0;

inline void mark(const char* what, const char* file, const char* func, uint16_t line) {
  TaskHandle_t cur = xTaskGetCurrentTaskHandle();
  uint8_t s = (cur == _tasks[LOOP]) ? LOOP : (cur == _tasks[FIREBASE]) ? FIREBASE : OTHER;
  if (s == OTHER) _otherTaskName = pcTaskGetName(cur);
  Crumb& c = _crumbs[s];
  c.what = what; c.file = file; c.func = func; c.line = line; c.since = millis();
}

// Call from the task itself, first thing — see Slot.
inline void registerTask(Slot s) { _tasks[s] = xTaskGetCurrentTaskHandle(); }
inline void beat()               { _loopBeat = millis(); }

// RAII: suspends freeze detection around a loop()-side call that may
// legitimately block for several seconds (HTTPS, WS connect).
struct Pause {
  Pause()  { _pauseDepth++; }
  ~Pause() { _pauseDepth--; _loopBeat = millis(); }
};

inline const char* _stateName(eTaskState s) {
  switch (s) {
    case eRunning:   return "Running";
    case eReady:     return "Ready";
    case eBlocked:   return "Blocked";
    case eSuspended: return "Suspended";
    case eDeleted:   return "Deleted";
    default:         return "?";
  }
}

inline const char* _baseName(const char* path) {
  if (!path) return "?";
  const char* b = path;
  for (const char* p = path; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
  return b;
}

inline int _appendCrumb(char* buf, size_t n, uint8_t s, uint32_t now) {
  const Crumb& c = _crumbs[s];
  const char* name = SLOT_NAMES[s];
  if (s == OTHER && _otherTaskName) name = _otherTaskName;

  const char* state = "-";
  if (s == LOOP && _tasks[LOOP]) state = _stateName(eTaskGetState(_tasks[LOOP]));
  if (s == FIREBASE) {
    // Only query a handle that's still the live task — Portal may have
    // vTaskDelete'd and recreated it since it registered.
    if (_tasks[FIREBASE] && _tasks[FIREBASE] == firebaseTaskHandle)
      state = _stateName(eTaskGetState(_tasks[FIREBASE]));
    else if (_tasks[FIREBASE])
      state = "gone";
  }

  if (!c.what) return snprintf(buf, n, "  %-9s (no marks yet)\n", name);
  return snprintf(buf, n, "  %-9s %-26s in %s() %s:%u  for %lu ms  state=%s\n",
                  name, c.what, c.func, _baseName(c.file), c.line,
                  (unsigned long)(now - c.since), state);
}

// Current breadcrumbs as text — also served live by GET /freeze.
inline String snapshot() {
  char buf[640];
  uint32_t now = millis();
  size_t len = 0;
  for (uint8_t s = 0; s < SLOT_COUNT && len < sizeof(buf); s++)
    len += _appendCrumb(buf + len, sizeof(buf) - len, s, now);
  if (len < sizeof(buf)) {
    TaskHandle_t holder = scoreMutex ? xSemaphoreGetMutexHolder(scoreMutex) : nullptr;
    snprintf(buf + len, sizeof(buf) - len, "  scoreMutex holder: %s\n",
             holder ? pcTaskGetName(holder) : "(free)");
  }
  return String(buf);
}

inline const char* _resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_SW:        return "software (esp_restart)";
    case ESP_RST_PANIC:     return "PANIC (crash)";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "other watchdog";
    case ESP_RST_BROWNOUT:  return "BROWNOUT (power dip)";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    case ESP_RST_EXT:       return "external reset";
    default:                return "other (e.g. USB reset after flashing)";
  }
}

inline uint32_t freezeCount() { return _freezeCount; }

// Crash / watchdog / power dip — anything other than a normal power-on,
// deliberate restart (incl. our own freeze reboot) or reset button.
inline bool abnormalReset() {
  switch (esp_reset_reason()) {
    case ESP_RST_PANIC: case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:   case ESP_RST_BROWNOUT:
      return true;
    default:
      return false;
  }
}

inline const char* resetReason() { return _resetReasonName(esp_reset_reason()); }

inline String status() {
  String s = "Reset reason this boot: ";
  s += _resetReasonName(esp_reset_reason());
  s += "\nFreezes recorded: ";
  s += _freezeCount;
  s += "\n\n--- Last freeze report ---\n";
  s += _lastReport[0] ? _lastReport : "(none)\n";
  s += "\n--- Live breadcrumbs ---\n";
  s += snapshot();
  return s;
}

inline void clearHistory() {
  Preferences p;
  p.begin("freezedbg", false);
  p.clear();
  p.end();
  _freezeCount = 0;
  _lastReport[0] = '\0';
}

static void _watchTask(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (_pauseDepth) continue;
    uint32_t now = millis();
    if (now - _loopBeat < STUCK_MS) continue;

    // Build the report straight into RTC memory first — if Serial itself is
    // what's wedged, it still survives the reboot below.
    size_t n = sizeof(_rtcReport), len = 0;
    len += snprintf(_rtcReport, n, "FREEZE at uptime %lus: loop() stalled for %lu ms, heap %u, firmware %s\n",
                    (unsigned long)(now / 1000), (unsigned long)(now - _loopBeat),
                    (unsigned)ESP.getFreeHeap(), FIRMWARE_VERSION);
    String snap = snapshot();
    if (len < n) snprintf(_rtcReport + len, n - len, "%s", snap.c_str());
    _rtcMagic = RTC_MAGIC;

    Serial.print("\n[FREEZE] ================================\n[FREEZE] ");
    Serial.print(_rtcReport);
    Serial.print("[FREEZE] rebooting in 3s\n[FREEZE] ================================\n");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
  }
}

// Call early in setup(), from the loop task, after Serial.begin().
inline void begin() {
  registerTask(LOOP);
  _loopBeat = millis();
  // The boot animation already drew before the loop task was registered,
  // leaving a stale "other" crumb under its name — forget it.
  _crumbs[OTHER] = Crumb{};
  _otherTaskName = nullptr;

  Preferences p;
  p.begin("freezedbg", false);
  if (_rtcMagic == RTC_MAGIC) {
    _rtcMagic = 0;
    _rtcReport[sizeof(_rtcReport) - 1] = '\0';
    p.putString("report", _rtcReport);
    p.putUInt("count", p.getUInt("count", 0) + 1);
    Serial.printf("[FREEZE] Previous boot ended in a freeze:\n%s", _rtcReport);
  }
  _freezeCount = p.getUInt("count", 0);
  p.getString("report", _lastReport, sizeof(_lastReport));
  p.end();

  Serial.printf("[BOOT] Reset reason: %s | freezes recorded: %lu (details: GET /freeze)\n",
                _resetReasonName(esp_reset_reason()), (unsigned long)_freezeCount);
}

// Call at the end of setup(), once the slow boot steps are done.
inline void startWatch() {
  _loopBeat = millis();
  xTaskCreatePinnedToCore(_watchTask, "freezewatch", 4096, nullptr, 5, nullptr, tskNO_AFFINITY);
}

} // namespace FreezeDebug

#define FD_MARK(what) FreezeDebug::mark((what), __FILE__, __func__, __LINE__)

#define SCORE_LOCK()   do { FD_MARK("waiting for scoreMutex"); \
                            xSemaphoreTake(scoreMutex, portMAX_DELAY); \
                            FD_MARK("holding scoreMutex"); } while (0)
#define SCORE_UNLOCK() do { xSemaphoreGive(scoreMutex); \
                            FD_MARK("released scoreMutex"); } while (0)

#define FD_SHOW()      do { FD_MARK("inside FastLED.show()"); \
                            FastLED.show(); \
                            FD_MARK("after FastLED.show()"); } while (0)
