#pragma once

// Copy this file to src/secrets.h and fill in your own values.
// src/secrets.h is gitignored — DO NOT commit it.

// ---- WiFi --------------------------------------------------------------
#define SEC_WIFI_SSID    "your-wifi-ssid"
#define SEC_WIFI_PASS    "your-wifi-password"

// ---- MQTT broker -------------------------------------------------------
#define SEC_MQTT_HOST    "192.168.1.10"
#define SEC_MQTT_PORT    1883
#define SEC_MQTT_USER    "mqtt-user"
#define SEC_MQTT_PASS    "mqtt-password"

// ---- OTA upload --------------------------------------------------------
//  Must match the --auth=... value in platformio.ini's pico_w_ota env.
#define SEC_OTA_HOSTNAME "vfd-dashboard"
#define SEC_OTA_PASSWORD "your-ota-password"

// ---- OctoPrint (Prusa page) -------------------------------------------
#define SEC_OCTO_HOST    "prusa.local"
#define SEC_OCTO_KEY     "your-octoprint-api-key"

// ---- Weather (Open-Meteo) ---------------------------------------------
//  Decimal degrees, e.g. "-37.64", "145.09".
#define SEC_WEATHER_LAT  "0.00"
#define SEC_WEATHER_LON  "0.00"
