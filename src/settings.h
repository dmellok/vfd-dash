#pragma once

#include <stdint.h>

struct UserSettings {
    uint8_t page = 0;
    uint8_t font = 0;
    uint8_t viz  = 0;
    uint8_t clk  = 0;
    uint8_t brightness = 128;
    uint8_t use12h     = 0;      // 0 = 24h, 1 = 12h
    uint8_t clockFont  = 0;
    uint8_t dashGlitch = 0;      // 0 = off, 1 = on (overlays static + glitches)
};

void settingsBegin();
UserSettings settingsLoad();
void settingsSave(const UserSettings& s);
