#pragma once
// Rename this file to `config.h` and fill in your environment-specific values.

// ====== Network / MQTT ======
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"

#define MQTT_HOST "mqtt.example.com"   // or e.g. "192.168.0.10"
#define MQTT_PORT 1883
#define MQTT_USER ""                   // no auth → empty string
#define MQTT_PASS ""
#define MQTT_CLIENT_ID "M5PaperS3-Dashboard"

// ====== Topics ======
#define TOPIC_RAIN      "home/weather/rain_sensor"
#define TOPIC_PICO      "sensor_data"
#define TOPIC_ENV4      "env4"
#define TOPIC_RPI_TEMP  "raspberry/temperature"
#define TOPIC_QZSS_TEMP "raspberry/qzss/temperature"
#define TOPIC_M5STICKC  "m5stickc_co2/co2_data"
#define TOPIC_M5CAPSULE "m5capsule/clients"

// ====== UI ======
#define DEVICE_TITLE "MQTT DASHBOARD"

// ====== Time / Refresh ======
#define ENABLE_NTP 1
#define TZ_INFO "JST-9"
#define NTP_SERVER1 "ntp.nict.jp"
#define NTP_SERVER2 "pool.ntp.org"
#define NTP_SERVER3 "time.google.com"

// Screen refresh interval (sec)
#define REFRESH_SEC 120
// Ghost reduction interval (sec)
#define GHOST_INTERVAL_SEC (2 * 3600)

// ====== Gauge ranges (1=enable / 0=disable) ======
// Rain
#define GAUGE_RAIN_CUR_ENABLE 0
#define GAUGE_RAIN_CUR_MIN 0
#define GAUGE_RAIN_CUR_MAX 4095

#define GAUGE_RAIN_BASE_ENABLE 0
#define GAUGE_RAIN_BASE_MIN 0
#define GAUGE_RAIN_BASE_MAX 4095

#define GAUGE_RAIN_UPTIME_ENABLE 1
#define GAUGE_RAIN_UPTIME_MIN 0
#define GAUGE_RAIN_UPTIME_MAX 38

// Pico W
#define GAUGE_PICO_TEMP_ENABLE 1
#define GAUGE_PICO_TEMP_MIN 0
#define GAUGE_PICO_TEMP_MAX 40

#define GAUGE_PICO_HUM_ENABLE 1
#define GAUGE_PICO_HUM_MIN 0
#define GAUGE_PICO_HUM_MAX 100

#define GAUGE_PICO_CO2_ENABLE 1
#define GAUGE_PICO_CO2_MIN 400
#define GAUGE_PICO_CO2_MAX 1500

#define GAUGE_PICO_THI_ENABLE 1
#define GAUGE_PICO_THI_MIN 50
#define GAUGE_PICO_THI_MAX 80

// Outside
#define GAUGE_OUT_TEMP_ENABLE 1
#define GAUGE_OUT_TEMP_MIN -10
#define GAUGE_OUT_TEMP_MAX 40

#define GAUGE_OUT_HUM_ENABLE 1
#define GAUGE_OUT_HUM_MIN 0
#define GAUGE_OUT_HUM_MAX 100

#define GAUGE_OUT_PRESS_ENABLE 1
#define GAUGE_OUT_PRESS_MIN 980
#define GAUGE_OUT_PRESS_MAX 1040

// System
#define GAUGE_RPI_TEMP_ENABLE 1
#define GAUGE_RPI_TEMP_MIN 30
#define GAUGE_RPI_TEMP_MAX 70

#define GAUGE_QZSS_TEMP_ENABLE 1
#define GAUGE_QZSS_TEMP_MIN 30
#define GAUGE_QZSS_TEMP_MAX 70

// Study room
#define GAUGE_STUDY_CO2_ENABLE 1
#define GAUGE_STUDY_CO2_MIN 400
#define GAUGE_STUDY_CO2_MAX 1500

#define GAUGE_STUDY_TEMP_ENABLE 1
#define GAUGE_STUDY_TEMP_MIN 0
#define GAUGE_STUDY_TEMP_MAX 40

#define GAUGE_STUDY_HUM_ENABLE 1
#define GAUGE_STUDY_HUM_MIN 0
#define GAUGE_STUDY_HUM_MAX 100

// THI band (optional)
#define THI_COOL_MAX 65
#define THI_COMFY_MAX 72
#define THI_WARM_MAX 78

// Stale detection
#define STALE_MINUTES 10  // e.g., 15
// Exclude indices from stale check (indices are defined in the main sketch)
#define STALE_EXCLUDE_LIST \
  { IDX_RAIN_STATE, IDX_RAIN_BASE, IDX_RAIN_CUR, IDX_RAIN_CABLE, IDX_STUDY_TEMP }

// ====== Seasonal Gauges (四季ごとゲージ範囲) ======
#define ENABLE_SEASONAL_GAUGES 1

// Season start dates (fixed MMDD; around Risshun/Rikka/Risshu/Rittou)
#define SEASON_SPRING_START_MMDD 204   // Feb 4
#define SEASON_SUMMER_START_MMDD 505   // May 5
#define SEASON_AUTUMN_START_MMDD 807   // Aug 7
#define SEASON_WINTER_START_MMDD 1107  // Nov 7

// ---- Outside Temperature ----
#define GAUGE_OUT_TEMP_MIN_SPRING -5
#define GAUGE_OUT_TEMP_MAX_SPRING 25
#define GAUGE_OUT_TEMP_MIN_SUMMER 15
#define GAUGE_OUT_TEMP_MAX_SUMMER 38
#define GAUGE_OUT_TEMP_MIN_AUTUMN -5
#define GAUGE_OUT_TEMP_MAX_AUTUMN 25
#define GAUGE_OUT_TEMP_MIN_WINTER -10
#define GAUGE_OUT_TEMP_MAX_WINTER 15

// ---- Indoor (Pico) Temperature ----
#define GAUGE_PICO_TEMP_MIN_SPRING 10
#define GAUGE_PICO_TEMP_MAX_SPRING 28
#define GAUGE_PICO_TEMP_MIN_SUMMER 20
#define GAUGE_PICO_TEMP_MAX_SUMMER 36
#define GAUGE_PICO_TEMP_MIN_AUTUMN 10
#define GAUGE_PICO_TEMP_MAX_AUTUMN 28
#define GAUGE_PICO_TEMP_MIN_WINTER 5
#define GAUGE_PICO_TEMP_MAX_WINTER 26

// ---- Study Temperature ----
#define GAUGE_STUDY_TEMP_MIN_SPRING 10
#define GAUGE_STUDY_TEMP_MAX_SPRING 28
#define GAUGE_STUDY_TEMP_MIN_SUMMER 20
#define GAUGE_STUDY_TEMP_MAX_SUMMER 36
#define GAUGE_STUDY_TEMP_MIN_AUTUMN 10
#define GAUGE_STUDY_TEMP_MAX_AUTUMN 28
#define GAUGE_STUDY_TEMP_MIN_WINTER 5
#define GAUGE_STUDY_TEMP_MAX_WINTER 26

// ---- THI (optional) ----
#define GAUGE_PICO_THI_MIN_SPRING 55
#define GAUGE_PICO_THI_MAX_SPRING 75
#define GAUGE_PICO_THI_MIN_SUMMER 60
#define GAUGE_PICO_THI_MAX_SUMMER 85
#define GAUGE_PICO_THI_MIN_AUTUMN 55
#define GAUGE_PICO_THI_MAX_AUTUMN 75
#define GAUGE_PICO_THI_MIN_WINTER 45
#define GAUGE_PICO_THI_MAX_WINTER 65

/* If you want seasonal ranges for other sensors (e.g., humidity), define similarly:
#define GAUGE_OUT_HUM_MIN_SPRING  20
#define GAUGE_OUT_HUM_MAX_SPRING  80
#define GAUGE_OUT_HUM_MIN_SUMMER  30
#define GAUGE_OUT_HUM_MAX_SUMMER  95
...
Then add a corresponding block in applySeasonalOverrides() in the main sketch.
*/
