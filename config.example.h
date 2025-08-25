#pragma once

// ====== Network / MQTT ======
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"

#define MQTT_HOST "192.168.1.10"
#define MQTT_PORT 1883
#define MQTT_USER ""  // 認証不要なら空文字
#define MQTT_PASS ""
#define MQTT_CLIENT_ID "M5PaperS3-Dashboard"

// ====== Topics ======
#define TOPIC_RAIN "home/weather/rain_sensor"
#define TOPIC_PICO "sensor_data"
#define TOPIC_ENV4 "env4"
#define TOPIC_RPI_TEMP "raspberry/temperature"
#define TOPIC_QZSS_TEMP "raspberry/qzss/temperature"
#define TOPIC_M5STICKC "m5stickc_co2/co2_data"
#define TOPIC_M5CAPSULE "m5capsule/clients"

// ====== UI ======
#define DEVICE_TITLE "MQTT DASHBOARD"

// ====== Time / Refresh ======
#define ENABLE_NTP 1
#define TZ_INFO "JST-9"
#define NTP_SERVER1 "ntp.nict.jp"
#define NTP_SERVER2 "pool.ntp.org"
#define NTP_SERVER3 "time.google.com"

// 画面の定期更新（秒）
#define REFRESH_SEC 120
// ゴースト軽減の周期（秒）
#define GHOST_INTERVAL_SEC (3 * 3600)

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
#define GAUGE_PICO_TEMP_MIN -10
#define GAUGE_PICO_TEMP_MAX 45

#define GAUGE_PICO_HUM_ENABLE 1
#define GAUGE_PICO_HUM_MIN 0
#define GAUGE_PICO_HUM_MAX 100

#define GAUGE_PICO_CO2_ENABLE 1
#define GAUGE_PICO_CO2_MIN 400
#define GAUGE_PICO_CO2_MAX 2000

#define GAUGE_PICO_THI_ENABLE 1
#define GAUGE_PICO_THI_MIN 50
#define GAUGE_PICO_THI_MAX 85

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
#define GAUGE_RPI_TEMP_MIN 20
#define GAUGE_RPI_TEMP_MAX 90

#define GAUGE_QZSS_TEMP_ENABLE 1
#define GAUGE_QZSS_TEMP_MIN 20
#define GAUGE_QZSS_TEMP_MAX 90

// Study room
#define GAUGE_STUDY_CO2_ENABLE 1
#define GAUGE_STUDY_CO2_MIN 400
#define GAUGE_STUDY_CO2_MAX 2000

#define GAUGE_STUDY_TEMP_ENABLE 1
#define GAUGE_STUDY_TEMP_MIN 0
#define GAUGE_STUDY_TEMP_MAX 40

#define GAUGE_STUDY_HUM_ENABLE 1
#define GAUGE_STUDY_HUM_MIN 0
#define GAUGE_STUDY_HUM_MAX 100

// THI band (任意)
#define THI_COOL_MAX 65
#define THI_COMFY_MAX 72
#define THI_WARM_MAX 78
