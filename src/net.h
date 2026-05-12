#pragma once

#include <Arduino.h>
#include <time.h>
#include "models.h"

// ---- Wi-Fi / lifecycle -------------------------------------------------
void netBegin();
void netLoop();
bool netConnected();
const char* netLocalIp();              // "0.0.0.0" when disconnected

// Restore cached cats + claude state from LittleFS so the dash has values
// to show before the first network round-trip completes.
void netLoadCache();

// OTA upload status — true while ArduinoOTA is mid-transfer. The progress
// percent is 0..100; -1 means an error has just been reported and we're
// briefly showing an error screen instead of progress.
bool netOtaActive();
int  netOtaProgressPct();

// ---- NTP time ----------------------------------------------------------
void timeBegin();
bool timeValid();                      // true once wall-clock is synced
bool getLocalTimeNow(struct tm& out);  // fills with Melbourne local time

// ---- MQTT --------------------------------------------------------------
typedef void (*CommandHandler)(const String&);
void mqttBegin(CommandHandler handler);
void mqttLoop();
bool mqttConnected();

// Publish a retained JSON state snapshot to the device's state topic. Used
// by main.cpp on settings changes and on a periodic heartbeat so HA always
// has an up-to-date view of the device.
void netPublishState(const char* json);

// ---- Now-playing (from MQTT) ------------------------------------------
const Song& getNowPlaying();

// ---- Weather (Open-Meteo) ---------------------------------------------
void weatherFetch();                   // blocking, call at most every few min
const Weather& getWeather();

// ---- Cats (The Daily Scoop) -------------------------------------------
void catsFetch();                      // blocking HTTP GET on /api/summary
const Cats& getCats();

// ---- Claude usage (MQTT-pushed) ---------------------------------------
const ClaudeUsage& getClaudeUsage();

// ---- OctoPrint (Prusa) -------------------------------------------------
void octoFetch();                      // blocking HTTP GET on /api/job + /api/printer
const OctoPrint& getOctoPrint();
