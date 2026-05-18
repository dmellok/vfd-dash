#include "net.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <time.h>

#include "cache.h"
#include "secrets.h"
#include "ui.h"

// ---- Config (secrets live in src/secrets.h — see secrets.example.h) ----
static const char*    WIFI_SSID = SEC_WIFI_SSID;
static const char*    WIFI_PASS = SEC_WIFI_PASS;

static const char*    MQTT_HOST = SEC_MQTT_HOST;
static const uint16_t MQTT_PORT = SEC_MQTT_PORT;
static const char*    MQTT_USER = SEC_MQTT_USER;
static const char*    MQTT_PASS = SEC_MQTT_PASS;
static const char*    MQTT_CLIENT_ID = "vfd-dashboard";
static const char*    TOPIC_PLAYING  = "straybot/playing";
static const char*    TOPIC_INPUT    = "vfd/input";
static const char*    TOPIC_CLAUDE   = "claude/usage";
static const char*    TOPIC_KNOB     = "keyboard/knob";
static const char*    TOPIC_STATE    = "vfd/state";
static const char*    TOPIC_AVAIL    = "vfd/availability";

// OTA — must also match upload_flags --auth=... in platformio.ini.
static const char* OTA_HOSTNAME = SEC_OTA_HOSTNAME;
static const char* OTA_PASSWORD = SEC_OTA_PASSWORD;

// Melbourne: AEST (UTC+10), AEDT (UTC+11) with DST starting first Sunday of
// October at 02:00 and ending first Sunday of April at 03:00 local.
static const char* MELBOURNE_TZ = "AEST-10AEDT,M10.1.0,M4.1.0/3";

static const char* WEATHER_LAT = SEC_WEATHER_LAT;
static const char* WEATHER_LON = SEC_WEATHER_LON;

// OctoPrint host + API key. Uses mDNS so the Pico must be on the same LAN.
static const char* OCTO_HOST = SEC_OCTO_HOST;
static const char* OCTO_KEY  = SEC_OCTO_KEY;

// ---- State -------------------------------------------------------------
static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);
static CommandHandler s_cmdHandler = nullptr;
static bool s_timeSynced = false;
static uint32_t s_lastMqttAttempt = 0;
static uint32_t s_lastWifiCheck = 0;

static Song s_song;
static Weather s_weather;
static Cats s_cats;
static ClaudeUsage s_claude;
static OctoPrint s_octo;

// OTA upload progress, populated from ArduinoOTA callbacks. -1 in
// s_otaProgressPct signals an error state (last upload attempt failed).
static volatile bool s_otaActive      = false;
static volatile int  s_otaProgressPct = 0;

// Parse a "YYYY-MM-DDTHH:MM:SS..." string into UTC epoch seconds. Anything
// after the seconds (fractional + timezone) is ignored — the API always
// provides the timestamp in UTC.
static time_t parseIsoUTC(const char* s) {
    if (!s || strlen(s) < 19) return 0;
    auto d2 = [&](int i) { return (s[i] - '0') * 10 + (s[i + 1] - '0'); };
    int y  = (s[0] - '0') * 1000 + (s[1] - '0') * 100
           + (s[2] - '0') * 10   + (s[3] - '0');
    int M  = d2(5);
    int d  = d2(8);
    int h  = d2(11);
    int mi = d2(14);
    int sc = d2(17);

    static const int kDaysBeforeMonth[] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    int leapDays = ((y - 1) / 4   - 1969 / 4)
                 - ((y - 1) / 100 - 1969 / 100)
                 + ((y - 1) / 400 - 1969 / 400);
    long days = (long)(y - 1970) * 365 + leapDays
              + kDaysBeforeMonth[M - 1] + (d - 1);
    bool curLeap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    if (curLeap && M > 2) days += 1;
    return (time_t)(days * 86400L + (long)h * 3600 + (long)mi * 60 + sc);
}

// ---- Wi-Fi -------------------------------------------------------------
void netBegin() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(OTA_HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(200);
    }
    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.setHostname(OTA_HOSTNAME);
        ArduinoOTA.setPassword(OTA_PASSWORD);
        ArduinoOTA.onStart([]() {
            s_otaActive      = true;
            s_otaProgressPct = 0;
            uiOtaTick();
        });
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            s_otaActive = true;
            if (total > 0) s_otaProgressPct = (int)((progress * 100UL) / total);
            uiOtaTick();
        });
        ArduinoOTA.onEnd([]() {
            s_otaProgressPct = 100;
            uiOtaTick();
            // Device reboots within milliseconds, leave the splash up.
        });
        ArduinoOTA.onError([](ota_error_t) {
            s_otaProgressPct = -1;
            uiOtaTick();
            // Stay "active" briefly so the error screen is visible; the next
            // upload attempt will reset state via onStart.
        });
        ArduinoOTA.begin();
        Serial.printf("[net] WiFi up ip=%s ota=%s.local\n",
                      WiFi.localIP().toString().c_str(), OTA_HOSTNAME);
    }
}

bool netOtaActive()      { return s_otaActive; }
int  netOtaProgressPct() { return s_otaProgressPct; }

void netLoop() {
    ArduinoOTA.handle();
    if (millis() - s_lastWifiCheck < 2000) return;
    s_lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
}

bool netConnected() { return WiFi.status() == WL_CONNECTED; }

void netLoadCache() {
    cacheLoadCats(s_cats);
    cacheLoadClaude(s_claude);
    s_octo.lastPrintingEpoch = cacheLoadLastPrint();
}

const char* netLocalIp() {
    static char buf[20] = "0.0.0.0";
    if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip = WiFi.localIP();
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    } else {
        strcpy(buf, "0.0.0.0");
    }
    return buf;
}

// ---- NTP ---------------------------------------------------------------
void timeBegin() {
    NTP.begin("pool.ntp.org", "time.google.com");
    setenv("TZ", MELBOURNE_TZ, 1);
    tzset();
    uint32_t start = millis();
    while (time(nullptr) < 1700000000 && millis() - start < 8000) {
        delay(200);
    }
    s_timeSynced = time(nullptr) > 1700000000;
}

bool timeValid() {
    if (s_timeSynced) return true;
    s_timeSynced = time(nullptr) > 1700000000;
    return s_timeSynced;
}

bool getLocalTimeNow(struct tm& out) {
    time_t now = time(nullptr);
    if (now < 1700000000) return false;
    localtime_r(&now, &out);
    return true;
}

// ---- MQTT --------------------------------------------------------------
static void parseClaudeUsage(const uint8_t* payload, unsigned int len) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, len)) return;

    s_claude.fiveHourPct       = doc["five_hour"]["utilization"]        | 0;
    s_claude.sevenDayPct       = doc["seven_day"]["utilization"]        | 0;
    s_claude.sevenDaySonnetPct = doc["seven_day_sonnet"]["utilization"] | 0;
    s_claude.extraEnabled      = doc["extra_usage"]["is_enabled"]       | false;
    s_claude.fiveHourResetUtc  = parseIsoUTC(doc["five_hour"]["resets_at"] | (const char*)nullptr);
    s_claude.sevenDayResetUtc  = parseIsoUTC(doc["seven_day"]["resets_at"] | (const char*)nullptr);
    s_claude.valid     = true;
    s_claude.updatedAt = millis();
    cacheSaveClaude(s_claude);
}

static void parsePlaying(const uint8_t* payload, unsigned int len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) return;

    s_song.isPlaying   = doc["is_playing"]  | false;
    s_song.durationMs  = doc["duration_ms"] | 0u;
    s_song.progressMs  = doc["progress_ms"] | 0u;
    s_song.capturedAt  = millis();
    s_song.name        = (const char*)(doc["name"]   | "");
    s_song.album       = (const char*)(doc["album"]  | "");
    s_song.artist      = (const char*)(doc["artist"] | "");
    s_song.everSeen    = true;
    s_song.updatedAt   = millis();
}

static void mqttCallback(char* topic, uint8_t* payload, unsigned int len) {
    if (strcmp(topic, TOPIC_PLAYING) == 0) {
        parsePlaying(payload, len);
    } else if (strcmp(topic, TOPIC_CLAUDE) == 0) {
        parseClaudeUsage(payload, len);
    } else if (strcmp(topic, TOPIC_KNOB) == 0) {
        if (s_cmdHandler) {
            String cmd; cmd.reserve(len);
            for (unsigned int i = 0; i < len; ++i) cmd += (char)payload[i];
            s_cmdHandler(cmd);
        }
    } else if (strcmp(topic, TOPIC_INPUT) == 0) {
        if (s_cmdHandler) {
            String cmd;
            cmd.reserve(len);
            for (unsigned int i = 0; i < len; ++i) cmd += (char)payload[i];
            s_cmdHandler(cmd);
        }
    }
}

void mqttBegin(CommandHandler handler) {
    s_cmdHandler = handler;
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCallback(mqttCallback);
    mqtt.setBufferSize(2048);
    mqtt.setKeepAlive(30);
}

static void mqttTryConnect() {
    if (!netConnected()) return;
    if (millis() - s_lastMqttAttempt < 5000) return;
    s_lastMqttAttempt = millis();
    // LWT publishes "offline" to the availability topic if the broker loses
    // contact with the device, so HA can mark it unavailable automatically.
    if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS,
                     TOPIC_AVAIL, 0, true, "offline")) {
        mqtt.publish(TOPIC_AVAIL, "online", true);
        mqtt.subscribe(TOPIC_PLAYING);
        mqtt.subscribe(TOPIC_INPUT);
        mqtt.subscribe(TOPIC_CLAUDE);
        mqtt.subscribe(TOPIC_KNOB);
    }
}

void netPublishState(const char* json) {
    if (!mqtt.connected()) return;
    mqtt.publish(TOPIC_STATE, json, /*retain=*/true);
}

void mqttLoop() {
    if (!mqtt.connected()) {
        mqttTryConnect();
        return;
    }
    mqtt.loop();
}

bool mqttConnected() { return mqtt.connected(); }

const Song& getNowPlaying() { return s_song; }

// ---- Weather -----------------------------------------------------------
void weatherFetch() {
    if (!netConnected()) return;

    String url = String("http://api.open-meteo.com/v1/forecast?latitude=")
               + WEATHER_LAT + "&longitude=" + WEATHER_LON
               + "&current=temperature_2m,apparent_temperature,"
                 "relative_humidity_2m,surface_pressure,weather_code,"
                 "cloud_cover,precipitation,wind_speed_10m,"
                 "wind_direction_10m,uv_index"
               + "&daily=temperature_2m_max,temperature_2m_min,"
                 "sunrise,sunset,precipitation_probability_max"
               + "&timezone=Australia%2FMelbourne&forecast_days=1";

    HTTPClient http;
    WiFiClient client;
    if (!http.begin(client, url)) return;
    http.setTimeout(6000);
    int code = http.GET();
    if (code != 200) { http.end(); return; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if (err) return;

    auto cur = doc["current"];
    auto day = doc["daily"];

    s_weather.tempC       = cur["temperature_2m"]        | 0.0f;
    s_weather.feelsLikeC  = cur["apparent_temperature"]  | 0.0f;
    s_weather.humidity    = cur["relative_humidity_2m"]  | 0;
    s_weather.pressure    = cur["surface_pressure"]      | 0.0f;
    s_weather.code        = cur["weather_code"]          | -1;
    s_weather.cloudCover  = cur["cloud_cover"]           | 0;
    s_weather.precipMm    = cur["precipitation"]         | 0.0f;
    s_weather.windKph     = cur["wind_speed_10m"]        | 0.0f;
    s_weather.windDir     = cur["wind_direction_10m"]    | 0;
    s_weather.uvIndex     = cur["uv_index"]              | 0.0f;

    s_weather.tempMaxC    = day["temperature_2m_max"][0] | 0.0f;
    s_weather.tempMinC    = day["temperature_2m_min"][0] | 0.0f;
    s_weather.precipProb  = day["precipitation_probability_max"][0] | 0;

    // Parse ISO8601 "YYYY-MM-DDTHH:MM" — pull hh/mm out of fixed offsets.
    auto parseIso = [](const char* iso, int& hh, int& mm) {
        if (!iso || strlen(iso) < 16) { hh = -1; mm = 0; return; }
        hh = (iso[11] - '0') * 10 + (iso[12] - '0');
        mm = (iso[14] - '0') * 10 + (iso[15] - '0');
    };
    parseIso(day["sunrise"][0] | (const char*)nullptr,
             s_weather.sunriseHour, s_weather.sunriseMin);
    parseIso(day["sunset"][0]  | (const char*)nullptr,
             s_weather.sunsetHour,  s_weather.sunsetMin);

    s_weather.valid     = (s_weather.code >= 0);
    s_weather.updatedAt = millis();
}

const Weather& getWeather() { return s_weather; }

// ---- Cats (The Daily Scoop) -------------------------------------------
static void fillCatStat(JsonObject obj, CatStat& c) {
    c.name            = (const char*)(obj["cat"] | "");
    c.recentWeight    = obj["recent_weight"]    | 0.0f;
    c.avgWeight       = obj["avg_weight"]       | 0.0f;
    c.minWeight       = obj["min_weight"]       | 0.0f;
    c.maxWeight       = obj["max_weight"]       | 0.0f;
    c.visitsToday     = obj["visits_today"]     | 0;
    c.totalVisits     = obj["total_visits"]     | 0;
    c.avgVisitsPerDay = obj["avg_visits_per_day"] | 0.0f;
    c.lastVisit       = (const char*)(obj["last_visit"] | "");
    c.peakHour        = (const char*)(obj["peak_hour"]  | "");
    c.mostActiveDow   = (const char*)(obj["most_active_dow"] | "");
    c.weightTrend     = (const char*)(obj["weight_trend"] | "");
    c.valid           = c.name.length() > 0;
}

void catsFetch() {
    if (!netConnected()) return;

    HTTPClient http;
    WiFiClient client;
    if (!http.begin(client, "http://prod:5554/api/summary")) return;
    http.setTimeout(6000);
    int code = http.GET();
    if (code != 200) { http.end(); return; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if (err) return;

    Cats fresh;
    for (JsonObject obj : doc.as<JsonArray>()) {
        CatStat c;
        fillCatStat(obj, c);
        if      (c.name.equalsIgnoreCase("Ada")) fresh.ada = c;
        else if (c.name.equalsIgnoreCase("Tux")) fresh.tux = c;
    }
    fresh.valid     = fresh.ada.valid || fresh.tux.valid;
    fresh.updatedAt = millis();
    if (fresh.valid) {
        s_cats = fresh;
        cacheSaveCats(s_cats);
    }
}

const Cats& getCats() { return s_cats; }

const ClaudeUsage& getClaudeUsage() { return s_claude; }

// ---- OctoPrint ---------------------------------------------------------
const OctoPrint& getOctoPrint() { return s_octo; }

void octoFetch() {
    if (!netConnected()) { s_octo.reachable = false; return; }

    auto callApi = [](const char* path, JsonDocument& doc) -> bool {
        HTTPClient http;
        WiFiClient client;
        String url = String("http://") + OCTO_HOST + path;
        if (!http.begin(client, url)) return false;
        http.setTimeout(4000);
        http.addHeader("X-Api-Key", OCTO_KEY);
        int code = http.GET();
        if (code != 200) { http.end(); return false; }
        bool ok = !deserializeJson(doc, http.getStream());
        http.end();
        return ok;
    };

    JsonDocument job, printer;
    if (!callApi("/api/job", job)) { s_octo.reachable = false; return; }
    s_octo.state            = (const char*)(job["state"] | "Unknown");
    s_octo.fileName         = (const char*)(job["job"]["file"]["name"] | "");
    s_octo.progressPct      = job["progress"]["completion"]    | 0.0f;
    s_octo.printTimeS       = job["progress"]["printTime"]     | 0L;
    s_octo.printTimeLeftS   = job["progress"]["printTimeLeft"] | -1L;
    s_octo.estimatedTotalS  = job["job"]["estimatedPrintTime"] | 0L;
    s_octo.filamentLenMm    = job["job"]["filament"]["tool0"]["length"] | 0L;
    s_octo.fileSizeBytes    = job["job"]["file"]["size"] | 0L;

    bool isPrinting = s_octo.state.startsWith("Printing")
                   || s_octo.state.startsWith("Pausing")
                   || s_octo.state.equalsIgnoreCase("Paused");
    static bool s_wasPrinting    = false;
    static uint32_t s_lastSaveMs = 0;
    time_t now = time(nullptr);
    if (isPrinting && now > 1700000000) {
        s_octo.lastPrintingEpoch = now;
        // Persist on first sighting of a print, on transition end, and every
        // 5 min during a long print to bound the post-crash error window.
        bool transition = !s_wasPrinting;
        bool periodic   = (millis() - s_lastSaveMs) > 5UL * 60 * 1000;
        if (transition || periodic) {
            cacheSaveLastPrint(s_octo.lastPrintingEpoch);
            s_lastSaveMs = millis();
        }
    } else if (s_wasPrinting && s_octo.lastPrintingEpoch > 0) {
        cacheSaveLastPrint(s_octo.lastPrintingEpoch);
        s_lastSaveMs = millis();
    }
    s_wasPrinting = isPrinting;

    // /api/printer can return 409 when the printer is offline — that's fine,
    // we still report job state.
    if (callApi("/api/printer", printer)) {
        auto t = printer["temperature"];
        s_octo.hotendActual = t["tool0"]["actual"] | 0.0f;
        s_octo.hotendTarget = t["tool0"]["target"] | 0.0f;
        s_octo.bedActual    = t["bed"]["actual"]   | 0.0f;
        s_octo.bedTarget    = t["bed"]["target"]   | 0.0f;
    }

    s_octo.valid     = true;
    s_octo.reachable = true;
    s_octo.updatedAt = millis();
}
