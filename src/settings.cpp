#include "settings.h"

#include <Arduino.h>
#include <LittleFS.h>

static const char* kPath = "/settings.bin";
static bool fsReady = false;

void settingsBegin() {
    if (fsReady) return;
    fsReady = LittleFS.begin();
}

UserSettings settingsLoad() {
    settingsBegin();
    UserSettings s;
    if (!fsReady) return s;
    File f = LittleFS.open(kPath, "r");
    if (!f) return s;
    uint8_t buf[8] = {0, 0, 0, 0, 128, 0, 0, 0};
    size_t n = f.read(buf, sizeof(buf));
    f.close();
    // Forward-compatible: older files may have fewer bytes.
    if (n >= 1) s.page       = buf[0];
    if (n >= 2) s.font       = buf[1];
    if (n >= 3) s.viz        = buf[2];
    if (n >= 4) s.clk        = buf[3];
    if (n >= 5) s.brightness = buf[4];
    if (n >= 6) s.use12h     = buf[5];
    if (n >= 7) s.clockFont  = buf[6];
    if (n >= 8) s.dashGlitch = buf[7];
    return s;
}

void settingsSave(const UserSettings& s) {
    settingsBegin();
    if (!fsReady) return;
    File f = LittleFS.open(kPath, "w");
    if (!f) return;
    uint8_t buf[8] = { s.page, s.font, s.viz, s.clk, s.brightness,
                       s.use12h, s.clockFont, s.dashGlitch };
    f.write(buf, sizeof(buf));
    f.close();
}
