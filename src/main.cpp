#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include "vfd.h"
#include "net.h"
#include "ui.h"
#include "settings.h"

static VFD vfd;
static uint8_t s_brightness = 128;

// Matrix-page "brightness mode": press toggles it; while active, up/down
// adjust brightness instead of cycling pages. Auto-exits after 15s of idle
// or when press is received again.
static bool     s_matrixBright = false;
static uint32_t s_matrixBrightUntil = 0;
static const uint32_t kMatrixBrightTimeoutMs = 15000;

// Display power. Driven by `display on/off/toggle` commands (typically from
// a presence sensor via HA). Off forces contrast to 0 while preserving the
// stored brightness so toggling back on returns to the same level.
static bool s_displayOn = true;

static void publishState(const char* why);   // forward decl

static void applyBrightness(uint8_t b) {
    s_brightness = b;
    vfd.setContrast(s_displayOn ? b : 0);
}

static void setDisplayOn(bool on) {
    if (s_displayOn == on) return;
    s_displayOn = on;
    vfd.setContrast(on ? s_brightness : 0);
    Serial.printf("[cmd] display %s\n", on ? "on" : "off");
    publishState(on ? "display on" : "display off");
}

// Simple linear lux → brightness curve. 0 lux floors at kMinB (still
// readable in a dark room), kCap+ lux saturates at kMaxB. HA can either
// publish raw lux via "lux N" and use this curve, or compute its own
// curve and publish a final brightness via "bright N".
static uint8_t luxToBrightness(int lux) {
    constexpr int kMinB = 24;
    constexpr int kMaxB = 255;
    constexpr int kCap  = 800;
    if (lux < 0)    lux = 0;
    if (lux > kCap) lux = kCap;
    int b = kMinB + (lux * (kMaxB - kMinB)) / kCap;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return (uint8_t)b;
}

static void publishState(const char* why) {
    if (!mqttConnected()) return;
    JsonDocument doc;
    doc["display"]      = s_displayOn ? "on" : "off";
    doc["brightness"]   = s_brightness;
    doc["page"]         = uiPageIndex();
    doc["page_name"]    = uiPageName(uiPageIndex());
    doc["font"]         = uiFontIndex();
    doc["viz"]          = uiVizIndex();
    doc["clock_face"]   = uiClockIndex();
    doc["clock_font"]   = uiClockFontIndex();
    doc["use_12h"]      = uiIs12h();
    doc["dash_glitch"]  = uiIsDashGlitch();
    doc["ip"]           = netLocalIp();
    doc["uptime_s"]     = (uint32_t)(millis() / 1000);
    doc["reason"]       = why ? why : "";
    char buf[320];
    serializeJson(doc, buf, sizeof(buf));
    netPublishState(buf);
}

static void persistAll(const char* why) {
    UserSettings s{ uiPageIndex(), uiFontIndex(), uiVizIndex(), uiClockIndex(),
                    s_brightness,
                    (uint8_t)(uiIs12h() ? 1 : 0),
                    uiClockFontIndex(),
                    (uint8_t)(uiIsDashGlitch() ? 1 : 0) };
    settingsSave(s);
    Serial.printf("[cmd] %s -> page=%u font=%u viz=%u clk=%u bright=%u 12h=%u cfont=%u glitch=%u\n",
                  why, s.page, s.font, s.viz, s.clk, s.brightness, s.use12h,
                  s.clockFont, s.dashGlitch);
    publishState(why);
}

static void onCommand(const String& raw) {
    String c = raw;
    c.trim();
    c.toLowerCase();

    // Brightness: absolute (`brightness 180` or `bright 180`) or relative.
    if (c.startsWith("bright") || c.startsWith("contrast")) {
        int sp = c.indexOf(' ');
        if (sp > 0) {
            long v = c.substring(sp + 1).toInt();
            if (v < 0) v = 0; if (v > 255) v = 255;
            applyBrightness((uint8_t)v);
            persistAll(c.c_str());
        }
        return;
    }
    if (c == "bup" || c == "bdn") {
        int delta = (c == "bup") ? 16 : -16;
        int v = (int)s_brightness + delta;
        if (v < 0) v = 0; if (v > 255) v = 255;
        applyBrightness((uint8_t)v);
        persistAll(c.c_str());
        return;
    }

    // Manually trigger a cat action: "cat <name>" (e.g. "cat meow").
    if (c.startsWith("cat ")) {
        String name = c.substring(4);
        name.trim();
        bool ok = uiTriggerCatAction(name.c_str());
        Serial.printf("[cmd] cat %s -> %s\n",
                      name.c_str(), ok ? "ok" : "unknown action");
        return;
    }

    // Display power: "display on" / "display off" / "display" (toggle).
    // Typically driven by a presence sensor via Home Assistant.
    if (c.startsWith("display")) {
        String arg = c.substring(7); arg.trim();
        bool target = s_displayOn;
        if      (arg == "on"  || arg == "1" || arg == "true")  target = true;
        else if (arg == "off" || arg == "0" || arg == "false") target = false;
        else if (arg == ""    || arg == "toggle")              target = !target;
        else { Serial.println("[cmd] display ?"); return; }
        setDisplayOn(target);
        return;
    }

    // Raw ambient lux from a light sensor. Mapped through the built-in
    // curve to brightness; not persisted (changes constantly).
    if (c.startsWith("lux ")) {
        long v = c.substring(4).toInt();
        if (v < 0) v = 0;
        applyBrightness(luxToBrightness((int)v));
        Serial.printf("[cmd] lux %ld -> brightness %u\n", v, s_brightness);
        return;
    }

    // Dash glitch toggle: "glitch on", "glitch off", "glitch toggle".
    if (c.startsWith("glitch")) {
        String arg = c.substring(6); arg.trim();
        bool target = uiIsDashGlitch();
        if      (arg == "on"  || arg == "1" || arg == "true")  target = true;
        else if (arg == "off" || arg == "0" || arg == "false") target = false;
        else if (arg == ""    || arg == "toggle")              target = !target;
        else { Serial.println("[cmd] glitch ?"); return; }
        uiSetDashGlitch(target);
        persistAll(c.c_str());
        return;
    }

    // keyboard/knob: rotary "up"/"down" cycles pages, "press" does a
    // context-sensitive thing depending on the current page.
    if (c == "up" || c == "down") {
        if (s_matrixBright && uiPageIndex() == 4) {
            int delta = (c == "up") ? 16 : -16;
            int v = (int)s_brightness + delta;
            if (v < 0) v = 0; if (v > 255) v = 255;
            applyBrightness((uint8_t)v);
            s_matrixBrightUntil = millis() + kMatrixBrightTimeoutMs;
            persistAll(c.c_str());
            return;
        }
        if (c == "up") uiNextPage(); else uiPrevPage();
        persistAll(c.c_str());
        return;
    }
    if (c == "press") {
        // Page-index constants must match the PageId enum order in ui.cpp:
        // 0 OVERVIEW, 1 TIME, 2 WEATHER, 3 NOW_PLAYING, 4 MATRIX,
        // 5 CATS, 6 TAMA, 7 CLAUDE, 8 PORTAL.
        uint8_t pg = uiPageIndex();
        switch (pg) {
            case 0:                 // OVERVIEW — toggle 12h/24h
                uiSet12h(!uiIs12h());
                break;
            case 3:                 // NOW PLAYING
                uiNextViz();
                break;
            case 1:                 // TIME / clock
                uiNextClock();
                break;
            case 4:                 // MATRIX — toggle brightness control mode
                s_matrixBright = !s_matrixBright;
                s_matrixBrightUntil = s_matrixBright
                    ? millis() + kMatrixBrightTimeoutMs : 0;
                return;
            default:
                return;             // press is a no-op on other pages
        }
        persistAll(c.c_str());
        return;
    }

    if      (c == "next")   uiNextPage();
    else if (c == "prev")   uiPrevPage();
    else if (c == "fnext")  uiNextFont();
    else if (c == "fprev")  uiPrevFont();
    else if (c == "vnext")  uiNextViz();
    else if (c == "vprev")  uiPrevViz();
    else if (c == "cnext")  uiNextClock();
    else if (c == "cprev")  uiPrevClock();
    else if (c == "cfnext") uiNextClockFont();
    else if (c == "cfprev") uiPrevClockFont();
    else if (c == "12h")    uiSet12h(true);
    else if (c == "24h")    uiSet12h(false);
    else return;

    persistAll(c.c_str());
}

static void pumpSerial() {
    static String buf;
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (buf.length()) { onCommand(buf); buf = ""; }
        } else if (buf.length() < 32) {
            buf += c;
        }
    }
}

void setup() {
    Serial.begin(115200);
    vfd.powerOnInit();

    UserSettings s = settingsLoad();
    uiSetDisplay(vfd);
    uiBegin(s.page, s.font, s.viz, s.clk);
    uiSetClockFont(s.clockFont);
    uiSet12h(s.use12h != 0);
    uiSetDashGlitch(s.dashGlitch != 0);
    applyBrightness(s.brightness);

    uiBoot(vfd, "Restoring cache...");
    netLoadCache();

    uiBoot(vfd, "Connecting WiFi...");
    netBegin();

    uiBoot(vfd, "Syncing time...");
    timeBegin();

    uiBoot(vfd, "Connecting MQTT...");
    mqttBegin(onCommand);

    uiBoot(vfd, "Fetching weather...");
    weatherFetch();

    uiBoot(vfd, "Polling Prusa...");
    octoFetch();
}

void loop() {
    netLoop();
    mqttLoop();
    pumpSerial();

    // Drop matrix brightness mode on idle timeout or if user navigated away.
    if (s_matrixBright &&
        ((int32_t)(millis() - s_matrixBrightUntil) >= 0 || uiPageIndex() != 4)) {
        s_matrixBright = false;
    }

    // Weather refresh every 10 minutes.
    static uint32_t lastWeather = millis();
    if (millis() - lastWeather > 600000UL) {
        weatherFetch();
        lastWeather = millis();
    }

    // Cats (The Daily Scoop) — refresh every minute.
    static uint32_t lastCats = 0;
    if (millis() - lastCats > 60000UL) {
        if (netConnected()) {
            catsFetch();
            lastCats = millis();
        }
    }

    // OctoPrint (Prusa) — refresh every 10 seconds while WiFi is up.
    static uint32_t lastOcto = 0;
    if (millis() - lastOcto > 10000UL) {
        if (netConnected()) {
            octoFetch();
            lastOcto = millis();
        }
    }

    // State heartbeat — republish the retained state every 30 s so HA has
    // a fresh snapshot, plus an immediate publish on first MQTT connect.
    static uint32_t lastState = 0;
    static bool wasMqttUp     = false;
    bool mqttUp = mqttConnected();
    if ((mqttUp && !wasMqttUp) || (mqttUp && millis() - lastState > 30000UL)) {
        publishState(wasMqttUp ? "heartbeat" : "boot");
        lastState = millis();
    }
    wasMqttUp = mqttUp;

    // Render at ~20 fps.
    static uint32_t lastDraw = 0;
    if (millis() - lastDraw >= 50) {
        lastDraw = millis();
        uiSetMatrixBrightProgress(s_matrixBright ? (s_brightness / 255.0f) : -1.0f);
        uiRender(vfd);
    }
}
