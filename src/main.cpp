#include <Arduino.h>

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

static void applyBrightness(uint8_t b) {
    s_brightness = b;
    vfd.setContrast(b);
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

    // Render at ~20 fps.
    static uint32_t lastDraw = 0;
    if (millis() - lastDraw >= 50) {
        lastDraw = millis();
        uiSetMatrixBrightProgress(s_matrixBright ? (s_brightness / 255.0f) : -1.0f);
        uiRender(vfd);
    }
}
