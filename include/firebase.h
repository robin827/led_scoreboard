/**
 * firebase.h - Lecture Firebase Realtime Database
 */

#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <vector>
#include <algorithm>
#include "config.h"
#include "score.h"
#include "team_names.h"

namespace Firebase {

static Preferences _prefs;
static String   _channelCache      = "";
static bool     _channelLoaded     = false;
static uint16_t _pollIntervalSec   = 3;
// See setChannel()/applyPendingChannel() below for why a channel change
// doesn't take effect on _channelCache immediately.
static String         _pendingChannel       = "";
static volatile bool  _channelChangePending = false;

// Shared SSL client — reused across read and write to avoid memory leaks
static WiFiClientSecure* _client = nullptr;

inline WiFiClientSecure* _getClient() {
  if (_client == nullptr) {
    _client = new WiFiClientSecure();
    _client->setInsecure();
    _client->setTimeout(10);
  }
  return _client;
}

inline void _resetClient() {
  delete _client;
  _client = nullptr;
}

// ── Account (Firebase Auth) ───────────────────────────────────────────────
// The RTDB rules (scoreboard/database.rules.json) only accept a write to
// match-<n> from a signed-in account that is an admin or whose org lists n in
// orgs/<org>/channels; reads stay public. The board signs in with an org
// account's email/password (ideally a dedicated boards account per org) and
// sends the resulting ID token as ?auth= on every request. Its courts
// (orgs/<org>/channels, every org's for an admin) feed the portal's court
// picker.
//
// Same token flow as the Firebase client SDKs: the password is used once, for
// signInWithPassword, and never stored — NVS keeps only the refresh token
// ("firebase"/"rtok"), which is exchanged for a fresh 1h ID token 5 minutes
// before expiry. Changing the account's password or disabling it in the
// Firebase Console revokes the refresh token, which signs the boards out.
//
// Threading: everything that touches the tokens runs on the Firebase task
// (Core 0) — tickAuth() at the top of its loop, _url() inside every request.
// The portal (Core 1) only hands new credentials over in RAM
// (_pendingEmail/_pendingPassword under _authMutex()) and raises
// _credsChanged; what it shows comes from _authStatusJson/_courtsJson.

static String   _idToken        = "";
static String   _refreshToken   = "";
static String   _email          = "";     // shown on the portal only
static uint32_t _nextAuthAt     = 0;      // millis() of the next refresh (5 min before expiry, or retry)
static bool     _authFailed     = false;  // last attempt failed — waiting out the retry delay
static bool     _authBlocked    = false;  // refresh token revoked/rejected — wait for a new sign-in
static volatile bool _credsChanged = true; // load the session from NVS on the first tick
static bool     _hasPending     = false;  // _pendingEmail/_pendingPassword hold a portal sign-in/out
static String   _pendingEmail   = "";
static String   _pendingPassword = "";

static String   _authStatusJson = "{\"state\":\"none\"}";
static String   _courtsJson     = "[]";
static uint32_t _courtsRev      = 0;

static const char* _AUTH_SIGNIN_URL  = "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" FIREBASE_API_KEY;
static const char* _AUTH_REFRESH_URL = "https://securetoken.googleapis.com/v1/token?key=" FIREBASE_API_KEY;

inline SemaphoreHandle_t _authMutex() {
  static SemaphoreHandle_t m = xSemaphoreCreateMutex();
  return m;
}

// state: "none" (no account), "pending", "ok", "error"
inline void _setAuthStatus(const char* state, const String& email, const String& error = "",
                           const String& org = "", const String& orgName = "",
                           const String& role = "") {
  JsonDocument doc;
  doc["state"] = state;
  if (email.length())    doc["email"]   = email;
  if (error.length())    doc["error"]   = error;
  if (org.length())      doc["org"]     = org;
  if (orgName.length())  doc["orgName"] = orgName;
  if (role.length())     doc["role"]    = role;
  String json;
  serializeJson(doc, json);
  xSemaphoreTake(_authMutex(), portMAX_DELAY);
  _authStatusJson = json;
  xSemaphoreGive(_authMutex());
}

inline void _setCourts(const String& json) {
  xSemaphoreTake(_authMutex(), portMAX_DELAY);
  if (json != _courtsJson) { _courtsJson = json; _courtsRev++; }
  xSemaphoreGive(_authMutex());
}

inline String getAuthStatusJson() {
  xSemaphoreTake(_authMutex(), portMAX_DELAY);
  String s = _authStatusJson;
  xSemaphoreGive(_authMutex());
  return s;
}

inline String getCourtsJson() {
  xSemaphoreTake(_authMutex(), portMAX_DELAY);
  String s = _courtsJson;
  xSemaphoreGive(_authMutex());
  return s;
}

inline uint32_t getCourtsRev() { return _courtsRev; }

// Portal side (Core 1): hand a sign-in (or, with an empty email, a sign-out)
// to the task. Kept in RAM only — if the board reboots before the task runs
// (e.g. not in FIREBASE mode), it has to be entered again.
inline void setCredentials(const String& email, const String& password) {
  xSemaphoreTake(_authMutex(), portMAX_DELAY);
  _pendingEmail    = email;
  _pendingPassword = password;
  _hasPending      = true;
  xSemaphoreGive(_authMutex());
  _credsChanged = true;
  if (email.length()) _setAuthStatus("pending", email);
  Serial.printf("[Firebase] Account set: %s\n", email.length() ? email.c_str() : "(none)");
}

// URL of match-<channel><subPath>.json, with the ID token when signed in.
// `query` is any extra query string without the leading '?'.
inline String _url(const String& channel, const String& subPath, const char* query = nullptr) {
  String url = String(FIREBASE_DATABASE_URL) + "/match-" + channel + subPath + ".json";
  char sep = '?';
  if (query && query[0]) { url += sep; url += query; sep = '&'; }
  if (_idToken.length()) { url += sep; url += "auth="; url += _idToken; }
  return url;
}

// A 401 on a write means the ID token expired or was revoked — refresh on the
// next tick instead of waiting for the scheduled renewal.
inline void _checkAuthCode(int code) {
  if (code == 401 && _idToken.length()) {
    Serial.println("[Firebase] Write rejected (401) — refreshing the token");
    if (!_authFailed) _nextAuthAt = millis();
  } else if (code == 401) {
    Serial.println("[Firebase] Write rejected (401) — no account set, see portal Settings → Firebase");
  }
}

inline int _httpsJson(const char* method, const String& url, const String& body, JsonDocument& out,
                      JsonDocument* filter = nullptr, const char* contentType = "application/json") {
  HTTPClient http;
  http.setTimeout(10000);
  http.setReuse(false);
  if (!http.begin(*_getClient(), url)) { _resetClient(); return -1; }
  int code;
  if (strcmp(method, "POST") == 0) {
    http.addHeader("Content-Type", contentType);
    code = http.POST(body);
  } else {
    code = http.GET();
  }
  if (code > 0) {
    String payload = http.getString();
    if (filter) deserializeJson(out, payload, DeserializationOption::Filter(*filter));
    else        deserializeJson(out, payload);
  }
  http.end();
  if (code < 0) _resetClient();
  return code;
}

inline void _saveSession() {
  _prefs.begin("firebase", false);
  _prefs.putString("email", _email);
  _prefs.putString("rtok", _refreshToken);
  _prefs.remove("pass");  // stored by the unreleased first version of this
  _prefs.end();
}

inline void _clearSession() {
  _idToken = "";
  _refreshToken = "";
  _prefs.begin("firebase", false);
  _prefs.remove("rtok");
  _prefs.remove("pass");
  _prefs.end();
}

struct _Court { String org; String orgName; int court; int channel; };

// Appends one org's courts (orgs/<org>/channels: {"<channel>": <court>}, which
// Firebase returns as an array indexed by channel when the keys are small
// numbers — both shapes handled) sorted by court.
inline void _collectCourts(const char* orgId, JsonVariantConst org, std::vector<_Court>& out) {
  String name = org["name"] | orgId;
  size_t first = out.size();
  JsonVariantConst ch = org["channels"];
  if (ch.is<JsonArrayConst>()) {
    int i = 0;
    for (JsonVariantConst v : ch.as<JsonArrayConst>()) {
      if (!v.isNull()) out.push_back({orgId, name, v.as<int>(), i});
      i++;
    }
  } else if (ch.is<JsonObjectConst>()) {
    for (JsonPairConst kv : ch.as<JsonObjectConst>()) {
      out.push_back({orgId, name, kv.value().as<int>(), atoi(kv.key().c_str())});
    }
  }
  std::sort(out.begin() + first, out.end(),
            [](const _Court& a, const _Court& b) { return a.court < b.court; });
}

inline void _authRetryLater(const String& msg) {
  _authFailed = true;
  _nextAuthAt = millis() + 30000;
  _setAuthStatus("error", _email, msg);
}

// After a new ID token: accounts/<uid> → orgs → courts. Runs on every
// refresh, so org/court changes in the console reach the board within an hour.
inline bool _loadAccount(const String& uid) {
  // Org and role. The account may read only its own entry.
  JsonDocument account;
  int code = _httpsJson("GET", String(FIREBASE_DATABASE_URL) + "/accounts/" + uid + ".json?auth=" + _idToken,
                        "", account);
  String org  = account["org"]  | "";
  String role = account["role"] | "";
  bool isAdmin = role == "admin";
  if (code != 200 || (org.isEmpty() && !isAdmin)) {
    String msg = code == 200 ? "Account has no org (accounts/" + uid + ")" : "Could not read account (HTTP " + String(code) + ")";
    Serial.printf("[Firebase] %s\n", msg.c_str());
    _authRetryLater(msg);
    _setCourts("[]");
    return false;  // token kept: writes are refused anyway without an org
  }

  // Courts: own org first, then (admins only) every other org by ID.
  JsonDocument orgs;
  code = _httpsJson("GET", String(FIREBASE_DATABASE_URL) + "/orgs.json", "", orgs);
  String orgName = org;
  std::vector<_Court> courts;
  if (code == 200) {
    if (org.length()) {
      orgName = orgs[org]["name"] | org.c_str();
      _collectCourts(org.c_str(), orgs[org], courts);
    }
    if (isAdmin) {
      std::vector<String> ids;
      for (JsonPairConst kv : orgs.as<JsonObjectConst>()) if (org != kv.key().c_str()) ids.push_back(kv.key().c_str());
      std::sort(ids.begin(), ids.end());
      for (const String& id : ids) _collectCourts(id.c_str(), orgs[id], courts);
    }
  }

  JsonDocument list;
  JsonArray arr = list.to<JsonArray>();
  for (const _Court& c : courts) {
    JsonObject o = arr.add<JsonObject>();
    o["org"] = c.org; o["orgName"] = c.orgName; o["court"] = c.court; o["channel"] = c.channel;
  }
  String courtsJson;
  serializeJson(list, courtsJson);
  _setCourts(courtsJson);

  _setAuthStatus("ok", _email, "", org, isAdmin && org.isEmpty() ? String("Admin") : orgName, isAdmin ? "admin" : "operator");
  Serial.printf("[Firebase] Signed in: org=%s role=%s, %d courts\n", org.c_str(), role.c_str(), (int)courts.size());
  return true;
}

inline void _gotIdToken(const String& idToken, long expiresIn) {
  _idToken    = idToken;
  _authFailed = false;
  _nextAuthAt = millis() + (uint32_t)max(60L, expiresIn - 300) * 1000UL;
}

// Portal sign-in: email/password → ID + refresh token. The password is not
// kept — the caller wipes it.
inline bool _signInWithPassword(const String& email, const String& password) {
  _email = email;
  _setAuthStatus("pending", _email);
  Serial.printf("[Firebase] Signing in as %s\n", email.c_str());

  JsonDocument req;
  req["email"] = email;
  req["password"] = password;
  req["returnSecureToken"] = true;
  String body;
  serializeJson(req, body);

  JsonDocument filter;
  filter["idToken"] = true;
  filter["refreshToken"] = true;
  filter["localId"] = true;
  filter["expiresIn"] = true;
  filter["error"]["message"] = true;
  JsonDocument res;
  int code = _httpsJson("POST", _AUTH_SIGNIN_URL, body, res, &filter);

  if (code != 200) {
    String msg = res["error"]["message"] | "";
    if (code < 0) msg = "No connection to Firebase Auth";
    else if (msg.isEmpty()) msg = "HTTP " + String(code);
    Serial.printf("[Firebase] Sign-in failed: %s\n", msg.c_str());
    if (msg == "INVALID_LOGIN_CREDENTIALS" || msg == "INVALID_PASSWORD" ||
        msg == "EMAIL_NOT_FOUND" || msg == "INVALID_EMAIL") msg = "Wrong email or password";
    else if (msg == "USER_DISABLED") msg = "Account disabled";
    else if (msg.startsWith("TOO_MANY_ATTEMPTS")) msg = "Too many attempts, try again later";
    // Nothing to retry with — the password is gone. Sign in again from the portal.
    _authBlocked = true;
    _setAuthStatus("error", _email, msg);
    return false;
  }

  _refreshToken = res["refreshToken"] | "";
  _saveSession();
  _gotIdToken(res["idToken"] | "", String(res["expiresIn"] | "3600").toInt());
  return _loadAccount(res["localId"] | "");
}

// Hourly renewal (and the first token after boot): refresh token → ID token.
inline bool _refreshIdToken() {
  JsonDocument filter;
  filter["id_token"] = true;
  filter["refresh_token"] = true;
  filter["user_id"] = true;
  filter["expires_in"] = true;
  filter["error"]["message"] = true;
  JsonDocument res;
  int code = _httpsJson("POST", _AUTH_REFRESH_URL,
                        "grant_type=refresh_token&refresh_token=" + _refreshToken, res, &filter,
                        "application/x-www-form-urlencoded");

  if (code != 200) {
    String msg = res["error"]["message"] | "";
    if (code < 0) msg = "No connection to Firebase Auth";
    else if (msg.isEmpty()) msg = "HTTP " + String(code);
    Serial.printf("[Firebase] Token refresh failed: %s\n", msg.c_str());
    // The session itself is gone (password changed, account disabled or
    // deleted): forget it. Anything else (network, 5xx) keeps the current ID
    // token — still valid ~5 minutes past the scheduled refresh — and retries.
    if (msg == "TOKEN_EXPIRED" || msg == "INVALID_REFRESH_TOKEN" ||
        msg == "USER_DISABLED" || msg == "USER_NOT_FOUND") {
      _clearSession();
      _authBlocked = true;
      _setAuthStatus("error", _email, msg == "USER_DISABLED" ? "Account disabled" : "Session ended — sign in again");
      _setCourts("[]");
    } else {
      _authRetryLater(msg);
    }
    return false;
  }

  String rt = res["refresh_token"] | "";
  if (rt.length() && rt != _refreshToken) { _refreshToken = rt; _saveSession(); }
  _gotIdToken(res["id_token"] | "", String(res["expires_in"] | "3600").toInt());
  return _loadAccount(res["user_id"] | "");
}

// Called by the Firebase task at the top of every loop iteration (online only).
inline void tickAuth() {
  if (_credsChanged) {
    _credsChanged = false;
    xSemaphoreTake(_authMutex(), portMAX_DELAY);
    bool   pending  = _hasPending;
    String email    = _pendingEmail;
    String password = _pendingPassword;
    _hasPending = false;
    _pendingEmail = "";
    _pendingPassword = "";
    xSemaphoreGive(_authMutex());

    _idToken     = "";
    _nextAuthAt  = millis();
    _authFailed  = false;
    _authBlocked = false;

    if (pending) {
      _clearSession();
      _email = email;
      if (email.isEmpty()) {
        _saveSession();  // stores the empty email
        _setAuthStatus("none", "");
        _setCourts("[]");
        Serial.println("[Firebase] Signed out");
        return;
      }
      _signInWithPassword(email, password);
      password = "";
      return;
    }

    // Boot: resume the stored session.
    _prefs.begin("firebase", true);
    _email        = _prefs.getString("email", "");
    _refreshToken = _prefs.getString("rtok", "");
    _prefs.end();
    if (_refreshToken.isEmpty()) {
      _setAuthStatus("none", "");
      _setCourts("[]");
      return;
    }
    _setAuthStatus("pending", _email);
  }
  if (_refreshToken.isEmpty() || _authBlocked) return;
  if ((int32_t)(millis() - _nextAuthAt) < 0) return;
  _refreshIdToken();
}

// ── Channel (match ID) ────────────────────────────────────────────────────

// Persists the new channel immediately, but deliberately does NOT update
// _channelCache here — this runs on the portal's HTTP handler (Core 1),
// while getChannel() is read from the Firebase task's own thread (Core 0),
// at essentially any point in the middle of a poll tick. Applying the
// change immediately let a tick already in progress pick up the NEW
// channel's data (since getChannel() would suddenly return it mid-tick)
// while still using the OLD channel's freshScoreRead/freshTimerRead/
// lastSeenRemoteTimerType bookkeeping — e.g. an ongoing timeout on the new
// channel would be seen as "some timer we don't recognize yet" and started
// at full duration (apply()'s path) for one tick, before the *next* tick's
// resync properly reset that bookkeeping and corrected it to the real
// remaining time. Reported live as the board flashing the wrong (full)
// countdown, then a bare score, then finally the correct countdown, all
// within about a second of switching channels.
// applyPendingChannel() — called by the Firebase task itself, atomically
// with that same bookkeeping reset (see main.cpp's requestFirebaseResync()
// handling) — is what actually makes the new channel visible to
// getChannel(), so no read can ever see "new channel, stale bookkeeping".
inline void setChannel(const String& matchId) {
  _pendingChannel       = matchId;
  _channelChangePending = true;
  _prefs.begin("firebase", false);
  _prefs.putString("channel", matchId);
  _prefs.end();
  Serial.printf("[Firebase] Channel set: %s\n", matchId.c_str());
}

// Called by the Firebase task (main.cpp's _firebaseRun()) as part of
// handling a resync request. No-op (returns false) if no channel change is
// pending, so it's safe to call on every resync regardless of cause.
inline bool applyPendingChannel() {
  if (!_channelChangePending) return false;
  _channelChangePending = false;
  _channelCache  = _pendingChannel;
  _channelLoaded = true;
  return true;
}

inline void loadPollInterval() {
  _prefs.begin("firebase", true);
  _pollIntervalSec = _prefs.getUShort("pollInt", 3);
  _prefs.end();
}

inline uint32_t getPollIntervalMs()  { return (uint32_t)_pollIntervalSec * 1000UL; }
inline uint16_t getPollIntervalSec() { return _pollIntervalSec; }

inline void setPollInterval(uint16_t secs) {
  _pollIntervalSec = secs;
  _prefs.begin("firebase", false);
  _prefs.putUShort("pollInt", secs);
  _prefs.end();
  Serial.printf("[Firebase] Poll interval: %ds\n", secs);
}

inline String getChannel() {
  if (!_channelLoaded) {
    _prefs.begin("firebase", true);
    _channelCache = _prefs.getString("channel", "");
    _prefs.end();
    _channelLoaded = true;
  }
  return _channelCache;
}

// ── Parse game_settings.set_mode into the firmware's own format enum ──────
// set_mode is a free-form string owned jointly by whichever writer last set
// it (this board, the manager server's firebaseBridge.js, or the friend's
// scoreboard tool) — values
// seen in practice include plain digit strings ("1"/"2"/"3", this board's
// own convention: total sets in the match) and labels like "Best of 3".
// Same parsing as firebaseBridge.js's setModeToFormat() so all readers agree
// on what a given string means.
inline uint8_t _parseSetMode(const String& raw) {
  String text = raw;
  text.trim();
  String lower = text;
  lower.toLowerCase();
  if (text == "1" || lower.indexOf("1 set") >= 0) return 0;
  if (text == "2" || lower.indexOf("2 set") >= 0) return 1;
  return 2; // default: Best of 3 (also covers "3", "Best of 3", unrecognized)
}

// Parses an RFC 1123 HTTP "Date" header ("Tue, 28 Sep 2026 20:14:00 GMT",
// always GMT per spec) into epoch milliseconds. Returns 0 on any parse
// failure. Hand-rolled rather than strptime/timegm since neither is
// reliably available across the ESP32 Arduino core's libc — used to learn
// the current wall-clock time from Firebase's own response instead of
// needing NTP (which needs UDP/123, not guaranteed open on venue WiFi; this
// reuses the HTTPS connection every Firebase read already makes).
// Returns int64_t, not long: `long` is 32-bit on ESP32 (same as `int`), and
// an epoch millisecond value (~1.7e12) overflows that long before the
// function even returns it — this only needs to fit an epoch in *seconds*
// (~1.7e9, fine in 32 bits), not milliseconds.
inline int64_t _parseHttpDateToEpochMs(const String& date) {
  if (date.length() < 29) return 0; // "Tue, 28 Sep 2026 20:14:00 GMT" == 29 chars
  int day  = date.substring(5, 7).toInt();
  String mon = date.substring(8, 11);
  int year = date.substring(12, 16).toInt();
  int hour = date.substring(17, 19).toInt();
  int mins = date.substring(20, 22).toInt();
  int sec  = date.substring(23, 25).toInt();

  static const char* MONTHS = "JanFebMarAprMayJunJulAugSepOctNovDec";
  int monIdx = -1;
  for (int i = 0; i < 12; i++) {
    if (mon[0] == MONTHS[i * 3] && mon[1] == MONTHS[i * 3 + 1] && mon[2] == MONTHS[i * 3 + 2]) {
      monIdx = i;
      break;
    }
  }
  if (monIdx < 0 || day < 1 || day > 31 || year < 2020) return 0;

  static const int16_t CUM_DAYS[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  int64_t days = 0;
  for (int y = 1970; y < year; y++) {
    bool yLeap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    days += yLeap ? 366 : 365;
  }
  days += CUM_DAYS[monIdx];
  if (monIdx > 1 && leap) days += 1;
  days += (day - 1);

  int64_t epochSec = days * 86400LL + hour * 3600LL + mins * 60LL + sec;
  return epochSec * 1000LL;
}

// ── Lecture du score depuis Firebase ──────────────────────────────────────

inline bool readScore(Score& score) {
  String channel = getChannel();
  if (channel.isEmpty()) {
    Serial.println("[Firebase] No channel configured");
    return false;
  }

  if (!WiFi.isConnected()) {
    Serial.println("[Firebase] Not connected");
    return false;
  }
  
  // Vérifie que l'IP STA est valide (pas 0.0.0.0)
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) {
    Serial.println("[Firebase] No valid IP");
    return false;
  }

  HTTPClient http;
  http.setTimeout(10000);
  http.setReuse(false);

  String url = _url(channel, "", "shallow=false");

  if (!http.begin(*_getClient(), url)) {
    Serial.println("[Firebase] Failed to begin HTTP (DNS error?)");
    _resetClient();
    return false;
  }

  int code = http.GET();

  if (code != 200) {
    Serial.printf("[Firebase] Failed to read match: %d\n", code);
    http.end();
    if (code < 0) {
      Serial.println("[Firebase] Network/SSL/DNS error, recreating client");
      _resetClient();
    }
    return false;
  }

  String payload = http.getString();
  http.end();

  // Parse active_set
  int idxActiveSet = payload.indexOf("\"active_set\":");
  if (idxActiveSet < 0) {
    Serial.println("[Firebase] No active_set found");
    return false;
  }
  int activeSet = payload.substring(idxActiveSet + 13).toInt();
  
  if (activeSet < 1) {
    Serial.println("[Firebase] Invalid active_set");
    return false;
  }

  // Parse le set actif
  String setKey = "\"set_" + String(activeSet) + "\":{";
  int idxSet = payload.indexOf(setKey);
  if (idxSet < 0) {
    Serial.printf("[Firebase] Set %d not found\n", activeSet);
    return false;
  }

  int searchStart = idxSet;
  int idxA = payload.indexOf("\"team_a_score\":", searchStart);
  int idxB = payload.indexOf("\"team_b_score\":", searchStart);

  if (idxA < 0 || idxB < 0) {
    Serial.println("[Firebase] Scores not found");
    return false;
  }

  score.scoreA = payload.substring(idxA + 15).toInt();
  score.scoreB = payload.substring(idxB + 15).toInt();

  // Parse game_settings.win_points and hardcap
  int idxWP = payload.indexOf("\"win_points\":");
  if (idxWP >= 0) {
    int valStart = idxWP + 13;
    while (valStart < (int)payload.length() && (payload[valStart] == ' ' || payload[valStart] == '"')) valStart++;
    int wp = payload.substring(valStart).toInt();
    if (wp >= 5 && wp <= 99) score.winPoints = wp;
  }
  int idxHC = payload.indexOf("\"hardcap\":");
  if (idxHC >= 0) {
    int valStart = idxHC + 10;
    while (valStart < (int)payload.length() && (payload[valStart] == ' ' || payload[valStart] == '"')) valStart++;
    int hc = payload.substring(valStart).toInt();
    if (hc == 0 || (hc >= 5 && hc <= 99)) score.hardcap = (uint8_t)hc;
  }
  // Parse game_settings.set_mode (this board's own "format": 0=BO1, 1=2 sets,
  // 2=BO3) — same node other tools call "Game Mode"/"set_mode", see
  // _parseSetMode() above for why the values aren't a simple number match.
  int idxSM = payload.indexOf("\"set_mode\":");
  if (idxSM >= 0) {
    int valStart = idxSM + 11;
    while (valStart < (int)payload.length() && payload[valStart] == ' ') valStart++;
    String smVal;
    if (valStart < (int)payload.length() && payload[valStart] == '"') {
      int q2 = payload.indexOf('"', valStart + 1);
      if (q2 > valStart) smVal = payload.substring(valStart + 1, q2);
    } else {
      int end = valStart;
      while (end < (int)payload.length() && payload[end] != ',' && payload[end] != '}') end++;
      smVal = payload.substring(valStart, end);
      smVal.trim();
    }
    if (smVal.length() > 0) score.format = _parseSetMode(smVal);
  }

  // Parse starting_server: "a"/"b" → Team A (firstServer=0), "c"/"d" → Team B (firstServer=1)
  int idxSS = payload.indexOf("\"starting_server\":", searchStart);
  if (idxSS >= 0) {
    int q1 = payload.indexOf('"', idxSS + 18) + 1;
    int q2 = payload.indexOf('"', q1);
    if (q1 > 0 && q2 > q1) {
      String sv = payload.substring(q1, q2);
      score.firstServer = (sv == "a" || sv == "b") ? 0 : 1;
    }
  }

  // Compte les sets gagnés (+ capture leur score final dans histA/histB)
  score.setA = 0;
  score.setB = 0;

  for (int i = 1; i < activeSet; i++) {
    String prevSetKey = "\"set_" + String(i) + "\":{";
    int prevIdx = payload.indexOf(prevSetKey);
    if (prevIdx >= 0) {
      int prevIdxA = payload.indexOf("\"team_a_score\":", prevIdx);
      int prevIdxB = payload.indexOf("\"team_b_score\":", prevIdx);

      if (prevIdxA >= 0 && prevIdxB >= 0) {
        int prevScoreA = payload.substring(prevIdxA + 15).toInt();
        int prevScoreB = payload.substring(prevIdxB + 15).toInt();

        if (prevScoreA > prevScoreB) {
          score.setA++;
        } else if (prevScoreB > prevScoreA) {
          score.setB++;
        }

        // Without this, `score` (the `db` passed in by firebaseTask) keeps
        // whatever histA/histB it started with — which, on a set transition
        // driven by an external writer rather than this board's own
        // nextSet(), is the PRE-transition (stale, possibly empty) history.
        // applyFromDatabase() then replaces currentScore wholesale with that
        // stale history, and this board's own next writeScore() rebuilds
        // score/set_<N> in Firebase straight from it — silently overwriting
        // the real completed-set score with 0 (or whatever was left in the
        // array). Capturing it here, from the exact same parse already used
        // to count setA/setB, keeps the board's history in sync with
        // whichever writer actually created the set.
        if (i - 1 < 3) {
          score.histA[i - 1] = (uint8_t)constrain(prevScoreA, 0, 99);
          score.histB[i - 1] = (uint8_t)constrain(prevScoreB, 0, 99);
        }
      }
    }
  }

  Serial.printf("[Firebase] Read OK: A=%d B=%d (sets %d-%d)\n",
    score.scoreA, score.scoreB, score.setA, score.setB);

  return true;
}

// ── Ecriture du score vers Firebase ───────────────────────────────────────
// starting_server/starting_receiver for the active set must be written in
// THIS SAME PATCH, not a separate request — writeScore's PATCH replaces the
// entire "score" node wholesale (Firebase PATCH semantics: a top-level key's
// value is written as-is, not deep-merged), so if this call only ever wrote
// team_a_score/team_b_score, every score update would silently wipe whatever
// starting_server the board (or anything else) had previously written for
// the active set — leaving the live-scores page's serve indicator missing
// on every ordinary point, not just occasionally. Same bug class already
// found and fixed in the manager server's firebaseBridge.js (see that
// file's "used to be two separate PATCH requests" note); this is the
// FIREBASE-mode (direct board→Firebase) equivalent, previously unfixed.

inline bool writeScore(const Score& score) {
  String channel = getChannel();
  if (channel.isEmpty()) {
    Serial.println("[Firebase] No channel configured");
    return false;
  }

  if (!WiFi.isConnected()) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) return false;

  HTTPClient http;
  http.setTimeout(5000);
  http.setReuse(false);

  String url = _url(channel, "");

  if (!http.begin(*_getClient(), url)) {
    Serial.println("[Firebase] Write: failed to begin HTTP");
    _resetClient();
    return false;
  }

  http.addHeader("Content-Type", "application/json");

  // Build payload: active_set + all historical sets + current set
  JsonDocument doc;
  int activeSet = score.setA + score.setB + 1;
  doc["active_set"] = activeSet;

  for (int i = 0; i < activeSet - 1; i++) {
    String key = "set_" + String(i + 1);
    doc["score"][key]["team_a_score"] = score.histA[i];
    doc["score"][key]["team_b_score"] = score.histB[i];
  }
  String currentKey = "set_" + String(activeSet);
  doc["score"][currentKey]["team_a_score"] = score.scoreA;
  doc["score"][currentKey]["team_b_score"] = score.scoreB;
  doc["score"][currentKey]["starting_server"]   = (score.firstServer == 0) ? "a" : "c";
  doc["score"][currentKey]["starting_receiver"] = (score.firstServer == 0) ? "c" : "a";

  String payload;
  serializeJson(doc, payload);

  int code = http.PATCH(payload);
  http.end();
  _checkAuthCode(code);

  if (code < 0) {
    Serial.printf("[Firebase] Write error: %d, recreating client\n", code);
    _resetClient();
    return false;
  }

  Serial.printf("[Firebase] Write OK: A=%d B=%d (set %d, code %d)\n",
    score.scoreA, score.scoreB, activeSet, code);
  return code == 200;
}

// ── Write starting_server + starting_receiver for the active set in one PATCH ──
// writeScore() above now also carries these fields on every score push, so
// this is only still needed for the standalone case: firstServer changes
// (or a new set starts) without scoreA/scoreB/setA/setB also changing on
// that same tick — main.cpp gates the call on exactly that condition.
// firstServer=0 (Team A / yellow): server="a", receiver="c"
// firstServer=1 (Team B / blue):   server="c", receiver="a"

inline bool writeFirstServer(const Score& score) {
  String channel = getChannel();
  if (channel.isEmpty()) return false;
  if (!WiFi.isConnected()) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) return false;

  int activeSet = score.setA + score.setB + 1;
  String url = _url(channel, "/score/set_" + String(activeSet));

  const char* serverVal   = (score.firstServer == 0) ? "a" : "c";
  const char* receiverVal = (score.firstServer == 0) ? "c" : "a";

  JsonDocument doc;
  doc["starting_server"]   = serverVal;
  doc["starting_receiver"] = receiverVal;
  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  http.setTimeout(5000);
  http.setReuse(false);
  if (!http.begin(*_getClient(), url)) { _resetClient(); return false; }
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(payload);
  http.end();
  _checkAuthCode(code);

  if (code < 0) { _resetClient(); return false; }
  Serial.printf("[Firebase] writeFirstServer OK: server=%s receiver=%s (code %d)\n",
    serverVal, receiverVal, code);
  return code == 200;
}

// ── Write hardcap ─────────────────────────────────────────────────────────────

inline bool writeHardcap(uint8_t hardcap) {
  String channel = getChannel();
  if (channel.isEmpty()) return false;
  if (!WiFi.isConnected()) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) return false;

  HTTPClient http;
  http.setTimeout(5000);
  http.setReuse(false);
  String url = _url(channel, "/game_settings/hardcap");
  if (!http.begin(*_getClient(), url)) { _resetClient(); return false; }
  http.addHeader("Content-Type", "application/json");
  int code = http.PUT(String(hardcap));
  http.end();
  _checkAuthCode(code);
  if (code < 0) { _resetClient(); return false; }
  Serial.printf("[Firebase] writeHardcap OK: %d (code %d)\n", hardcap, code);
  return code == 200;
}

// ── Write win_points directly to its leaf node (avoids overwriting siblings) ──

inline bool writeWinPoints(uint8_t winPoints) {
  String channel = getChannel();
  if (channel.isEmpty()) return false;
  if (!WiFi.isConnected()) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) return false;

  HTTPClient http;
  http.setTimeout(5000);
  http.setReuse(false);

  String url = _url(channel, "/game_settings/win_points");

  if (!http.begin(*_getClient(), url)) { _resetClient(); return false; }
  http.addHeader("Content-Type", "application/json");
  int code = http.PUT(String(winPoints));
  http.end();
  _checkAuthCode(code);

  if (code < 0) { _resetClient(); return false; }
  Serial.printf("[Firebase] writeWinPoints OK: %d (code %d)\n", winPoints, code);
  return code == 200;
}

// ── Write format (as game_settings.set_mode, the shared "Game Mode" field) ──
// Written as a plain digit string ("1"/"2"/"3" = total sets in the match) —
// the same convention the former Fwango bridge wrote via numberOfGames —
// so every existing reader (friend's tool, firebaseBridge.js) parses it
// correctly without needing to special-case this board as a writer.

inline bool writeFormat(uint8_t format) {
  String channel = getChannel();
  if (channel.isEmpty()) return false;
  if (!WiFi.isConnected()) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) return false;

  const char* setMode = (format == 0) ? "1" : (format == 1) ? "2" : "3";

  HTTPClient http;
  http.setTimeout(5000);
  http.setReuse(false);
  String url = _url(channel, "/game_settings/set_mode");
  if (!http.begin(*_getClient(), url)) { _resetClient(); return false; }
  http.addHeader("Content-Type", "application/json");
  int code = http.PUT("\"" + String(setMode) + "\"");
  http.end();
  _checkAuthCode(code);

  if (code < 0) { _resetClient(); return false; }
  Serial.printf("[Firebase] writeFormat OK: %s (code %d)\n", setMode, code);
  return code == 200;
}

// ── Shared timer (timeout/medical/break) state ───────────────────────────────
// Mirrors the RTDB "match-<channel>/timer" node also written by the manager
// server's firebaseBridge.js for CENTRAL-mode boards (see that file's
// pushUpToFirebase/pushDownToBoard) and read/written by scoreboard/'s
// input.html + index.html. No duration field on purpose: every reader keeps
// its own copy of the same fixed durations this board already hardcodes in
// score_actions.h (BREAK/TIMEOUT/MEDICAL_*_MS) — RTDB only ever carries
// *which* timer (if any) is running, never how long it lasts.
// Timers are passed around as timer keys ("timeout", "a/timeout",
// "b/medical", "break" — same strings as ScoreActions::activeTimerKey() and
// its apply() commands): the "a/"/"b/" prefix maps to the node's optional
// `team: "a"|"b"`, the rest to `type`.

inline bool writeTimerState(const char* key) {
  const char* type = key;
  const char* team = nullptr;
  if (key && (key[0] == 'a' || key[0] == 'b') && key[1] == '/') {
    team = (key[0] == 'a') ? "a" : "b";
    type = key + 2;
  }
  String channel = getChannel();
  if (channel.isEmpty()) return false;
  if (!WiFi.isConnected()) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) return false;

  HTTPClient http;
  http.setTimeout(5000);
  http.setReuse(false);
  String url = _url(channel, "/timer");
  if (!http.begin(*_getClient(), url)) { _resetClient(); return false; }
  http.addHeader("Content-Type", "application/json");

  int code;
  if (type == nullptr || type[0] == '\0') {
    code = http.PUT("null"); // deletes the whole "timer" node
  } else {
    JsonDocument doc;
    doc["type"] = type;
    if (team) doc["team"] = team;
    doc["started_at"][".sv"] = "timestamp"; // Firebase server-timestamp sentinel
    String payload;
    serializeJson(doc, payload);
    // PUT (not PATCH): replaces the node wholesale so a stale started_at from
    // a previous timer of the same type never lingers under the new one.
    code = http.PUT(payload);
  }
  http.end();
  _checkAuthCode(code);

  if (code < 0) { _resetClient(); return false; }
  Serial.printf("[Firebase] writeTimerState OK: %s (code %d)\n", (key && key[0]) ? key : "(none)", code);
  return code == 200;
}

// Returns true on a successful read, with outType set to the timer key
// ("timeout"/"medical"/"break", prefixed "a/"/"b/" when the node has a
// `team`), or "" when no timer is currently active in Firebase. outElapsedMs
// is how long that timer has already been running, computed from the node's
// "started_at" (a Firebase server timestamp) against this same response's
// HTTP "Date" header — both timestamps come from Firebase's own frontend, so
// no local wall-clock/NTP sync is needed. Set to -1 (unknown) when outType
// is "" or when either timestamp couldn't be read/parsed — callers that
// can't trust an elapsed time should treat that the same as "just don't
// know", not as "0 elapsed".
inline bool readTimerState(String& outType, long& outElapsedMs) {
  outElapsedMs = -1;
  String channel = getChannel();
  if (channel.isEmpty()) return false;
  if (!WiFi.isConnected()) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) return false;

  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);
  String url = _url(channel, "/timer");
  if (!http.begin(*_getClient(), url)) { _resetClient(); return false; }
  const char* headerKeys[] = {"Date"};
  http.collectHeaders(headerKeys, 1);
  int code = http.GET();
  if (code != 200) {
    http.end();
    if (code < 0) _resetClient();
    return false;
  }
  String payload = http.getString();
  String dateHeader = http.header("Date");
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err || doc.isNull()) {
    outType = ""; // "null" (no timer) or unexpected/unparseable shape
    return true;
  }
  outType = String((const char*)(doc["type"] | ""));
  String team = String((const char*)(doc["team"] | ""));
  if ((outType == "timeout" || outType == "medical") && (team == "a" || team == "b")) {
    outType = team + "/" + outType;
  }
  if (outType.length() > 0 && doc["started_at"].is<int64_t>()) {
    int64_t startedAt  = doc["started_at"];
    int64_t serverNow  = _parseHttpDateToEpochMs(dateHeader);
    int64_t elapsed    = serverNow - startedAt;
    // Cap at ~24 days so this always fits the 32-bit `long` outElapsedMs is
    // declared as — real elapsed times here are at most a few minutes
    // (BREAK/TIMEOUT/MEDICAL durations), this is just a sanity clamp against
    // a clock-skew/parse edge case producing a huge or negative delta.
    if (serverNow > 0 && startedAt > 0 && elapsed >= 0 && elapsed < 2000000000LL) {
      outElapsedMs = (long)elapsed;
    }
  }
  return true;
}

// ── Write team/player names ──────────────────────────────────────────────────
// PATCHes only match-<channel>/meta/team_a (or team_b), and only the
// name/player_1/player_2 leaves that are actually non-empty — meta also has
// other writers (roundnet-worlds-live's editor UI and Fwango bridge) and this
// must never overwrite color/country/phase/court/status, nor clobber a name
// set elsewhere with a blank one just because this board hasn't been given
// one yet.

inline bool _writeTeamSide(const String& channel, const char* side,
                            const char* name, const char* p1, const char* p2) {
  JsonDocument doc;
  bool any = false;
  if (name[0]) { doc["name"]     = name; any = true; }
  if (p1[0])   { doc["player_1"] = p1;   any = true; }
  if (p2[0])   { doc["player_2"] = p2;   any = true; }
  if (!any) return true; // nothing to push for this side — not an error

  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  http.setTimeout(5000);
  http.setReuse(false);
  String url = _url(channel, String("/meta/") + side);
  if (!http.begin(*_getClient(), url)) { _resetClient(); return false; }
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(payload);
  http.end();
  _checkAuthCode(code);

  if (code < 0) {
    Serial.printf("[Firebase] writeTeamNames(%s) network error: %d\n", side, code);
    _resetClient();
    return false;
  }
  if (code != 200) {
    Serial.printf("[Firebase] writeTeamNames(%s) FAILED, HTTP %d\n", side, code);
    return false;
  }
  Serial.printf("[Firebase] writeTeamNames(%s) OK\n", side);
  return true;
}

inline bool writeTeamNames(const TeamNames::Names& n) {
  String channel = getChannel();
  if (channel.isEmpty()) return false;
  if (!WiFi.isConnected()) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) return false;

  bool okA = _writeTeamSide(channel, "team_a", n.teamA, n.playerA1, n.playerA2);
  bool okB = _writeTeamSide(channel, "team_b", n.teamB, n.playerB1, n.playerB2);
  return okA && okB;
}

// ── Read team/player names ───────────────────────────────────────────────────
// Reads match-<channel>/teams_info (an official feed some tournaments already
// populate — not written by any code in this repo) and match-<channel>/meta
// (this project's own, editor-mode-writable fallback), and fills `out` with
// team_a/team_b's name/player_1/player_2 — preferring teams_info when a field
// is present there, exactly like roundnet-worlds-live's index.html already
// resolves display names (see deriveMatch()'s teamView()). Without this
// precedence, a channel with real tournament data in teams_info but nothing
// in meta (e.g. channel 6/7) would look empty to the board even though the
// live hub already shows names for it.

inline bool _fetchJson(const String& channel, const char* path, JsonDocument& doc) {
  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);
  String url = _url(channel, String("/") + path);
  if (!http.begin(*_getClient(), url)) { _resetClient(); return false; }
  int code = http.GET();
  if (code != 200) {
    http.end();
    if (code < 0) _resetClient();
    return false;
  }
  String payload = http.getString();
  http.end();
  return deserializeJson(doc, payload) == DeserializationError::Ok;
}

inline String _pickTeamField(bool haveOfficial, JsonDocument& official,
                              bool haveMeta, JsonDocument& meta,
                              const char* side, const char* key) {
  if (haveOfficial) {
    const char* v = official[side][key] | (const char*)nullptr;
    if (v && v[0]) return String(v);
  }
  if (haveMeta) {
    const char* v = meta[side][key] | (const char*)nullptr;
    if (v && v[0]) return String(v);
  }
  return String("");
}

inline bool readTeamNames(TeamNames::Names& out) {
  String channel = getChannel();
  if (channel.isEmpty()) return false;
  if (!WiFi.isConnected()) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0) return false;

  JsonDocument official, meta;
  bool haveOfficial = _fetchJson(channel, "teams_info", official);
  bool haveMeta     = _fetchJson(channel, "meta", meta);
  if (!haveOfficial && !haveMeta) return false;

  strlcpy(out.teamA,    _pickTeamField(haveOfficial, official, haveMeta, meta, "team_a", "name").c_str(),     sizeof(out.teamA));
  strlcpy(out.playerA1, _pickTeamField(haveOfficial, official, haveMeta, meta, "team_a", "player_1").c_str(), sizeof(out.playerA1));
  strlcpy(out.playerA2, _pickTeamField(haveOfficial, official, haveMeta, meta, "team_a", "player_2").c_str(), sizeof(out.playerA2));
  strlcpy(out.teamB,    _pickTeamField(haveOfficial, official, haveMeta, meta, "team_b", "name").c_str(),     sizeof(out.teamB));
  strlcpy(out.playerB1, _pickTeamField(haveOfficial, official, haveMeta, meta, "team_b", "player_1").c_str(), sizeof(out.playerB1));
  strlcpy(out.playerB2, _pickTeamField(haveOfficial, official, haveMeta, meta, "team_b", "player_2").c_str(), sizeof(out.playerB2));
  return true;
}

} // namespace Firebase