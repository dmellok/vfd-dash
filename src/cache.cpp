#include "cache.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static const char* kCatsPath      = "/cats.json";
static const char* kClaudePath    = "/claude.json";
static const char* kLastPrintPath = "/lastprint.bin";
static bool fsReady = false;

static void ensureFs() {
    if (fsReady) return;
    fsReady = LittleFS.begin();
}

static bool readJson(const char* path, JsonDocument& doc) {
    ensureFs();
    if (!fsReady) return false;
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    return !err;
}

static void writeJson(const char* path, const JsonDocument& doc) {
    ensureFs();
    if (!fsReady) return;
    File f = LittleFS.open(path, "w");
    if (!f) return;
    serializeJson(doc, f);
    f.close();
}

// ---- Cats --------------------------------------------------------------
static void packCat(JsonObject obj, const CatStat& c) {
    obj["nm"] = c.name;
    obj["rw"] = c.recentWeight;
    obj["aw"] = c.avgWeight;
    obj["mn"] = c.minWeight;
    obj["mx"] = c.maxWeight;
    obj["vt"] = c.visitsToday;
    obj["tv"] = c.totalVisits;
    obj["av"] = c.avgVisitsPerDay;
    obj["lv"] = c.lastVisit;
    obj["pk"] = c.peakHour;
    obj["dw"] = c.mostActiveDow;
    obj["wt"] = c.weightTrend;
    obj["v"]  = c.valid;
}

static void unpackCat(JsonObjectConst obj, CatStat& c) {
    c.name            = (const char*)(obj["nm"] | "");
    c.recentWeight    = obj["rw"] | 0.0f;
    c.avgWeight       = obj["aw"] | 0.0f;
    c.minWeight       = obj["mn"] | 0.0f;
    c.maxWeight       = obj["mx"] | 0.0f;
    c.visitsToday     = obj["vt"] | 0;
    c.totalVisits     = obj["tv"] | 0;
    c.avgVisitsPerDay = obj["av"] | 0.0f;
    c.lastVisit       = (const char*)(obj["lv"] | "");
    c.peakHour        = (const char*)(obj["pk"] | "");
    c.mostActiveDow   = (const char*)(obj["dw"] | "");
    c.weightTrend     = (const char*)(obj["wt"] | "");
    c.valid           = obj["v"]  | false;
}

void cacheSaveCats(const Cats& cats) {
    JsonDocument doc;
    packCat(doc["a"].to<JsonObject>(), cats.ada);
    packCat(doc["t"].to<JsonObject>(), cats.tux);
    doc["v"] = cats.valid;
    writeJson(kCatsPath, doc);
}

void cacheLoadCats(Cats& out) {
    JsonDocument doc;
    if (!readJson(kCatsPath, doc)) return;
    unpackCat(doc["a"].as<JsonObjectConst>(), out.ada);
    unpackCat(doc["t"].as<JsonObjectConst>(), out.tux);
    out.valid = doc["v"] | (out.ada.valid || out.tux.valid);
}

// ---- Claude ------------------------------------------------------------
void cacheSaveClaude(const ClaudeUsage& u) {
    JsonDocument doc;
    doc["f5"] = u.fiveHourPct;
    doc["d7"] = u.sevenDayPct;
    doc["s7"] = u.sevenDaySonnetPct;
    doc["xt"] = u.extraEnabled;
    doc["fr"] = (long)u.fiveHourResetUtc;
    doc["dr"] = (long)u.sevenDayResetUtc;
    doc["v"]  = u.valid;
    writeJson(kClaudePath, doc);
}

void cacheLoadClaude(ClaudeUsage& out) {
    JsonDocument doc;
    if (!readJson(kClaudePath, doc)) return;
    out.fiveHourPct        = doc["f5"] | 0;
    out.sevenDayPct        = doc["d7"] | 0;
    out.sevenDaySonnetPct  = doc["s7"] | 0;
    out.extraEnabled       = doc["xt"] | false;
    out.fiveHourResetUtc   = (time_t)(doc["fr"] | 0L);
    out.sevenDayResetUtc   = (time_t)(doc["dr"] | 0L);
    out.valid              = doc["v"]  | false;
}

// ---- Last-print timestamp ----------------------------------------------
// Stored as raw bytes (no parsing overhead) — just sizeof(time_t) bytes.
time_t cacheLoadLastPrint() {
    ensureFs();
    if (!fsReady) return 0;
    File f = LittleFS.open(kLastPrintPath, "r");
    if (!f) return 0;
    time_t epoch = 0;
    f.read((uint8_t*)&epoch, sizeof(epoch));
    f.close();
    return epoch;
}

void cacheSaveLastPrint(time_t epoch) {
    ensureFs();
    if (!fsReady) return;
    File f = LittleFS.open(kLastPrintPath, "w");
    if (!f) return;
    f.write((const uint8_t*)&epoch, sizeof(epoch));
    f.close();
}
