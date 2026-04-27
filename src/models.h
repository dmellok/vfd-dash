#pragma once

#include <Arduino.h>

struct Song {
    bool isPlaying = false;
    uint32_t durationMs = 0;
    uint32_t progressMs = 0;        // last-received progress
    uint32_t capturedAt = 0;        // millis() at time of last-received progress
    String name;
    String album;
    String artist;
    bool everSeen = false;          // true once any payload has arrived
    uint32_t updatedAt = 0;

    uint32_t currentProgressMs() const {
        if (!isPlaying) return progressMs;
        uint32_t now = millis();
        uint32_t p = progressMs + (now - capturedAt);
        if (durationMs && p > durationMs) p = durationMs;
        return p;
    }
};

struct CatStat {
    String  name;
    float   recentWeight = 0;
    float   avgWeight    = 0;
    float   minWeight    = 0;
    float   maxWeight    = 0;
    int     visitsToday  = 0;
    int     totalVisits  = 0;
    float   avgVisitsPerDay = 0;
    String  lastVisit;          // "YYYY-MM-DD HH:MM"
    String  peakHour;           // e.g. "7 am"
    String  mostActiveDow;      // e.g. "Tue"
    String  weightTrend;        // e.g. "+0.01 kg"
    bool    valid = false;
};

struct Cats {
    CatStat ada;
    CatStat tux;
    bool    valid = false;
    uint32_t updatedAt = 0;
};

// Claude usage payload, published to MQTT topic `claude/usage`. The reset
// timestamps are kept as UTC epoch seconds so the page can show countdowns
// without re-parsing the ISO string every frame.
struct ClaudeUsage {
    int     fiveHourPct          = 0;
    int     sevenDayPct          = 0;
    int     sevenDaySonnetPct    = 0;
    bool    extraEnabled         = false;
    time_t  fiveHourResetUtc     = 0;
    time_t  sevenDayResetUtc     = 0;
    bool    valid                = false;
    uint32_t updatedAt           = 0;
};

// OctoPrint job + printer state. Fetched over HTTP from /api/job and
// /api/printer with the configured X-Api-Key.
struct OctoPrint {
    String   state;          // "Printing", "Operational", "Paused", ...
    String   fileName;       // current (or last) job file
    float    progressPct = 0;
    long     printTimeS  = 0;     // elapsed seconds
    long     printTimeLeftS = -1; // remaining seconds, -1 when unknown
    long     estimatedTotalS = 0; // slicer estimate (whole job), 0 = unknown
    long     filamentLenMm  = 0;  // total filament length for the job (mm)
    long     fileSizeBytes  = 0;  // gcode file size
    float    hotendActual = 0;
    float    hotendTarget = 0;
    float    bedActual    = 0;
    float    bedTarget    = 0;
    bool     valid        = false;   // any successful fetch ever
    bool     reachable    = false;   // most recent fetch succeeded
    uint32_t updatedAt    = 0;
    time_t   lastPrintingEpoch = 0;  // UTC seconds when state was last printing/paused
};

struct Weather {
    float tempC       = 0;
    float feelsLikeC  = 0;
    float tempMaxC    = 0;
    float tempMinC    = 0;
    float windKph     = 0;
    int   windDir     = 0;    // degrees (0 = N, 90 = E, ...)
    int   humidity    = 0;    // %
    float pressure    = 0;    // hPa
    int   cloudCover  = 0;    // %
    float precipMm    = 0;    // current hour mm
    int   precipProb  = 0;    // max probability today (%)
    float uvIndex     = 0;
    int   sunriseHour = -1;
    int   sunriseMin  = 0;
    int   sunsetHour  = -1;
    int   sunsetMin   = 0;
    int   code        = -1;   // WMO weather code
    bool  valid       = false;
    uint32_t updatedAt = 0;
};
