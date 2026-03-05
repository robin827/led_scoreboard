# Roundnet Scoreboard — ESP32-S3

LED scoreboard system for roundnet based on ESP32-S3 with LED display, captive WiFi portal, and optional Firebase synchronization.

---

## 📋 Overview

### Hardware
- **Microcontroller**: ESP32-S3 (dual-core, 4MB flash)
- **Display**: 24×8 LED matrix (WS2812B), driven by FastLED
- **Physical controls**: 12mm buttons + EC11 rotary encoder (planned)
- **Enclosure**: White 3D printed (reduces solar heat absorption)

### Software architecture
- **Core 0**: Firebase task (polling READ mode)
- **Core 1**: Captive portal + WebServer + DNS + WiFi + LEDs
- **Dual-mode WiFi**: AP (hotspot) + STA (internet connection) simultaneously

---

## 🎮 Operating Modes

### LOCAL Mode
- Manual score control via captive portal
- `RN-Score` WiFi hotspot active
- **No internet connection required**
- Autonomous and stable
- Interface: +1/-1 buttons, Next Set, Reset Match

### READ Mode
- Reads score from Firebase Realtime Database
- Automatic polling every X seconds
- WiFi hotspot remains active for portal access
- Internet connection via WiFi scan (list of available networks)
- Display of connected SSID/signal
- Score controls grayed out (read-only)

---

## 🔧 Configuration

### Firebase Realtime Database

**Database URL:**
```cpp
#define FIREBASE_DATABASE_URL "https://live-scoreboard-fc0e5-default-rtdb.europe-west1.firebasedatabase.app"
```

**Expected data structure:**
```
match-{N}/
  ├─ active_set: 1
  ├─ score/
  │   ├─ set_1/
  │   │   ├─ team_a_score: 0
  │   │   └─ team_b_score: 0
  │   └─ set_2/
  │       ├─ team_a_score: 0
  │       └─ team_b_score: 0
  ├─ phase: "semi FINAL"
  └─ format: "BO3 21 - HC 25"
```

**Firebase security rules:**
```json
{
  "rules": {
    ".read": true,
    ".write": true
  }
}
```
*(To be secured in production)*

### WiFi

**AP Hotspot:**
- SSID: `RN-Score`
- Password: `roundnet`
- IP: `192.168.4.1`

**STA Configuration:**
- Network scan via portal
- Credentials saved in NVS
- Limited retry: 3 attempts × 15s = 45s max
- Automatic shutdown on failure to preserve the AP

---

## 🎨 LED Display

### 24×8 Matrix
- **Resolution**: 24 columns × 8 rows = 192 LEDs
- **Wiring**: Vertical zigzag, starting from bottom-right
- **Mapping**: Custom `xy(x, y)` function (see `led.h`)

### Character font
- **Digits**: 4×7 pixels per digit
- **Layout**:
  - Team A: tens + units (9px) — position 2-10
  - Center gap: 2px
  - Team B: tens + units (9px) — position 13-21
  - Total centered: 20px over 24px available

### Set indicators
- **Team A**: Pixels 1-2 on row 7 (orange)
- **Team B**: Pixels 21-22 on row 7 (blue)

### Colors
- Team A: `#FF6B35` (orange)
- Team B: `#4A9EFF` (blue)

---

## 🌐 Captive Portal

### Modern web interface
- **Design**: Dark theme, mobile-responsive
- **URL**: `http://192.168.4.1`
- **Auto-detection**: Android/iOS routes (`/generate_204`, `/hotspot-detect.html`)

### Contextual sections
- **Mode selector**: LOCAL / READ (always visible)
- **Channel (Match ID)**: visible only in READ mode
- **WiFi scan**: visible only in READ mode
- **Brightness**: always visible (1-255, saved in NVS)

### Features
- Real-time score display
- +1/-1 controls per team
- Next Set / Reset Match
- LOCAL/READ mode selection
- WiFi network scan with RSSI
- WiFi connect/disconnect
- Brightness adjustment with slider

---

## 🔥 Firebase — READ Mode

### Critical optimizations
- **Reused SSL client**: Avoids memory leaks
- **Single request**: Reads the entire match at once (`?shallow=false`)
- **Timeout**: 10s to avoid blocking
- **Smart retry**:
  - Interval: 5s on success, 15s on error
  - 30s pause after 5 consecutive errors
  - SSL client recreation on DNS error

### Score reading
1. Reads `active_set` to determine the current set
2. Parses `set_N/team_a_score` and `team_b_score`
3. Counts sets won (rule: ≥21 with a 2-point margin)
4. Updates LEDs only if a change is detected

---

## ⚙️ Initialization Order (CRITICAL)

The initialization order is **essential** to avoid memory crashes:

```cpp
1. Mode::init()              // Loads LOCAL/READ from NVS
2. WiFiMgr::init()           // AP + STA attempt (with delays)
   delay(1000);              // Critical WiFi stabilization
3. Portal::init()            // WebServer (created dynamically AFTER WiFi)
4. LED::init()               // FastLED LAST (avoids heap corruption)
5. xTaskCreatePinnedToCore() // Firebase task on Core 0
```

**Why:**
- WiFi must start before FastLED (heap conflict)
- WebServer created after stable WiFi (avoids crashes)
- LEDs last (heavy initialization)

---

## 🐛 Known Issues and Solutions

### 1. AP WiFi disappears after STA connection
**Cause**: WiFi mode switches from `WIFI_AP_STA` to `WIFI_STA`

**Solution**: `WiFiMgr::tick()` checks and forces the mode at each cycle

### 2. Cascading DNS errors (`start_ssl_client: -1`)
**Cause**: Repeated Firebase requests fail, crashing the WiFi stack

**Solution**:
- Limited retry with exponential backoff
- 30s pause after 5 consecutive errors
- SSL client recreation on error

### 3. Captive portal does not open automatically (Android)
**Cause**: Missing detection routes

**Solution**: Routes `/generate_204`, `/hotspot-detect.html`, `/canonical.html`

### 4. Heap corruption at boot
**Cause**: FastLED initialized before WiFi

**Solution**: Follow the critical initialization order

### 5. Corrupted NVS (broken WiFi credentials)
**Solution**:
- Reflash LOCAL version (clears NVS)
- Or command: `pio run --target erase`
- Or HTTP route: `http://192.168.4.1/wifi/reset`

---

## 📦 Project Structure

```
minimal_scoreboard/
├── main.cpp           # Entry point, setup(), loop(), Firebase task
├── config.h           # Constants (WiFi, Firebase, LED, timeouts)
├── mode.h             # LOCAL/READ mode management (NVS)
├── score.h            # Score structure + increment/nextSet/reset logic
├── led.h              # FastLED, 4×7 font, xy() function, brightness NVS
├── wifi_mgr.h         # WiFi AP+STA, scan, NVS credentials, retry
├── firebase.h         # Realtime DB reading, reused SSL client
├── portal.h           # WebServer, DNS, embedded HTML, API routes
└── platformio.ini     # ESP32-S3 config, huge_app partition, USB CDC
```

---

## 🔑 Persistent Data (NVS)

| Namespace  | Key         | Type   | Description                     |
|------------|-------------|--------|---------------------------------|
| `mode`     | `mode`      | uint8  | 0=LOCAL, 1=READ                 |
| `wifi`     | `ssid`      | string | WiFi network SSID               |
| `wifi`     | `pass`      | string | WiFi password                   |
| `firebase` | `channel`   | string | Match ID (e.g.: "15")           |
| `led`      | `brightness`| uint8  | LED brightness (1-255)          |

**Clear all NVS**:
```cpp
#include <nvs_flash.h>
nvs_flash_erase();
nvs_flash_init();
```

---

## 🚀 Build and Upload

### PlatformIO
```bash
pio run --target upload --target monitor
```

### Partition scheme
- **huge_app.csv** (3MB app, required for the full firmware)
- Flash size: **4MB** (verify hardware matches)

### ESP32-S3 USB CDC configuration
```ini
build_flags = -DARDUINO_USB_CDC_ON_BOOT=1
```
Critical delay: `delay(2000)` before `Serial.begin(115200)` for CDC stabilization

---

## 📝 Development Notes

### Key principles
- **Initialization order is critical**: WiFi → Portal → LEDs
- **Firebase via REST, not full library**: Reduces memory footprint
- **Dual-core isolation**: Firebase (Core 0) never blocks the portal (Core 1)
- **NVS for all persistence**: Mode, WiFi, Channel, Brightness
- **Static SSL client**: Reused to avoid memory leaks

### Tools and resources
- **Platform**: PlatformIO, ESP32-S3
- **Libraries**: FastLED, WebServer, WiFiManager, HTTPClient, ArduinoJson, Preferences
- **Cloud**: Firebase Realtime Database (REST API)
- **Hardware**: PBS-12B 12mm buttons, EC11 encoder, WS2812B LEDs

### Patterns to avoid
- ❌ Creating WebServer before stable WiFi
- ❌ Initializing FastLED before WiFi
- ❌ Flooding DNS with looping requests
- ❌ Creating a new WiFiClientSecure on every request
- ❌ Using the default partition (too small)

---

## 🎯 Future Roadmap (optional)

- [ ] BLE bracelet integration (central + peripheral role)
- [ ] Firebase write mode (LOCAL + cloud sync)
- [ ] Physical buttons + rotary encoder
- [ ] OTA updates via GitHub Releases
- [ ] Advanced LED animations (celebrations, timeouts)
- [ ] Multi-match support (quick selection)

---

## 📞 Support

In case of issues:

1. **Check the Serial Monitor** (detailed logs at each step)
2. **Clear the NVS** if behavior is erratic
3. **Switch to LOCAL mode** if Firebase is unstable
4. **Verify the initialization order** if crashes at boot
5. **Consult the "Known Issues" sections** above

**Real-time monitoring**:
- WiFi log every 10s: mode, AP IP, STA IP, free heap
- Firebase log before/after request: heap before, heap after
- Online/Offline badge in the portal

---

*Last updated: March 2026*
*Developed in French, conversations with Claude (Anthropic)*
