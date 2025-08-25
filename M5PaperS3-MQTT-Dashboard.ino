/* M5PaperS3 MQTT Dashboard (ONE-PAGE layout, 24/7-hardened + centered title)
    - No paging: always fits into a single page
    - Density modes: Normal → Compact → Ultra (auto pick to fit)
    - Hide empty gauges and expand value column
    - Section headers with breathing space (auto-scaled)
    - "Today at a glance" card drawn only if space remains
    - Footer: 2 centered lines (Help / Last MQTT update)
    - Binary gauges for Rain / Cable (positive ≈70% fill)
    - Value column: show full; if overflow & numeric-like → round to 2 decimals
    - 24/7 hardening:
        * Wi-Fi auto-reconnect with backoff
        * MQTT reconnect with backoff, tuned keepalive/socket timeout/buffer
        * Optional watchdog (ESP32 WDT) + periodic health checks
        * Periodic NTP refresh
*/

#include <M5Unified.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#ifdef ARDUINO_ARCH_ESP32
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_task_wdt.h>
#include <esp_idf_version.h>
#endif
#include "config.h"

// ───────── Optional NTP (override in config.h) ─────────
#ifndef ENABLE_NTP
#define ENABLE_NTP 1
#endif
#ifndef TZ_INFO
#define TZ_INFO "JST-9"
#endif
#ifndef NTP_SERVER1
#define NTP_SERVER1 "ntp.nict.jp"
#endif
#ifndef NTP_SERVER2
#define NTP_SERVER2 "pool.ntp.org"
#endif
#ifndef NTP_SERVER3
#define NTP_SERVER3 "time.google.com"
#endif

// ───────── 24/7 options (override in config.h if needed) ─────────
#ifndef ENABLE_WATCHDOG
#define ENABLE_WATCHDOG 1
#endif
#ifndef WATCHDOG_TIMEOUT_SEC
#define WATCHDOG_TIMEOUT_SEC 15
#endif
#ifndef WIFI_MAX_BACKOFF_MS
#define WIFI_MAX_BACKOFF_MS 60000UL
#endif
#ifndef MQTT_MAX_BACKOFF_MS
#define MQTT_MAX_BACKOFF_MS 60000UL
#endif
#ifndef HEALTH_CHECK_INTERVAL_MS
#define HEALTH_CHECK_INTERVAL_MS 30000UL
#endif
#ifndef LOW_HEAP_RESTART_KB
#define LOW_HEAP_RESTART_KB 40
#endif
#ifndef NTP_REFRESH_INTERVAL_MS
#define NTP_REFRESH_INTERVAL_MS 6UL * 60UL * 60UL * 1000UL
#endif

// ───────── Drawing constants ─────────
M5Canvas canvas(&M5.Display);
static constexpr uint32_t COLOR_BG = 0xFFFFFFu;
static constexpr uint32_t COLOR_FG = 0x000000u;
static constexpr uint32_t COLOR_DIM = 0x666666u;
static constexpr uint32_t COLOR_LINE = 0xDDDDDDu;

// padding
static constexpr int LEFT_PAD = 16;
static constexpr int RIGHT_PAD = 16;

// backoff helper（Arduinoのmin競合を避ける自前キャップ）
static inline uint32_t cap_u32(uint32_t v, uint32_t vmax) {
  return (v > vmax) ? vmax : v;
}

// Density styles (auto selected to fit 1 page)
struct LayoutStyle {
  int HEADER_H;
  int FOOTER_H;
  int LINE_H;
  int SEC_EXTRA;
  int GAUGE_W;
  int GAUGE_H;
  int VALUE_COL_W;
  int GAP_COL;
  int GLANCE_H;
  const lgfx::IFont* TITLE_FONT;
  const lgfx::IFont* LABEL_FONT;
  const lgfx::IFont* VALUE_FONT;
  const lgfx::IFont* SEC_FONT;
  const lgfx::IFont* SMALL_FONT;
};
static const LayoutStyle STYLE_NORMAL = {
  64, 56, 38, 12, 160, 18, 100, 10, 112,
  &fonts::lgfxJapanGothic_24, &fonts::lgfxJapanGothic_20, &fonts::lgfxJapanGothic_24, &fonts::Font4, &fonts::Font2
};
static const LayoutStyle STYLE_COMPACT = {
  58, 54, 34, 8, 150, 16, 96, 8, 100,
  &fonts::lgfxJapanGothic_24, &fonts::lgfxJapanGothic_16, &fonts::lgfxJapanGothic_20, &fonts::Font4, &fonts::Font2
};
static const LayoutStyle STYLE_ULTRA = {
  54, 52, 30, 6, 144, 14, 92, 8, 90,
  &fonts::lgfxJapanGothic_20, &fonts::lgfxJapanGothic_16, &fonts::lgfxJapanGothic_16, &fonts::Font2, &fonts::Font2
};
LayoutStyle L = STYLE_NORMAL;

// ───────── MQTT / Sensors ─────────
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// Backoff & timers
static uint32_t wifiLastTry = 0, wifiBackoff = 1000;
static uint32_t mqttLastTry = 0, mqttBackoff = 1000;
static uint32_t lastHealth = 0;
static uint32_t lastNtpSync = 0;

struct SensorItem {
  const char* label;
  String value;
};
enum : uint8_t {
  IDX_RAIN_STATE,
  IDX_RAIN_CUR,
  IDX_RAIN_BASE,
  IDX_RAIN_UPTIME,
  IDX_RAIN_CABLE,
  IDX_PICO_TEMP,
  IDX_PICO_HUM,
  IDX_PICO_CO2,
  IDX_PICO_THI,
  IDX_OUT_TEMP,
  IDX_OUT_HUM,
  IDX_OUT_PRESS,
  IDX_RPI_TEMP,
  IDX_QZSS_TEMP,
  IDX_STUDY_CO2,
  IDX_STUDY_TEMP,
  IDX_STUDY_HUM,
  IDX_M5CAP_CLIENTS,
  SENSOR_COUNT
};
SensorItem sensors[SENSOR_COUNT] = {
  { "雨", "-" }, { "雨 現在値(ADC)", "-" }, { "雨 ベースライン", "-" }, { "雨 稼働時間(h)", "-" }, { "雨 ケーブル", "-" }, { "Pico 温度(°C)", "-" }, { "Pico 湿度(%)", "-" }, { "リビング CO2(ppm)", "-" }, { "Pico THI", "-" }, { "外 温度(°C)", "-" }, { "外 湿度(%)", "-" }, { "外 気圧(hPa)", "-" }, { "RPi5 CPU(°C)", "-" }, { "QZSS CPU(°C)", "-" }, { "書斎 CO2(ppm)", "-" }, { "書斎 温度(°C)", "-" }, { "書斎 湿度(%)", "-" }, { "M5Capsule Clients", "-" }
};

// Section rows
struct Row {
  int8_t sensor;
  const char* header;
};
static const Row rows[] = {
  { -1, "RAIN" },
  { IDX_RAIN_STATE, nullptr },
  { IDX_RAIN_CUR, nullptr },
  { IDX_RAIN_BASE, nullptr },
  { IDX_RAIN_UPTIME, nullptr },
  { IDX_RAIN_CABLE, nullptr },
  { -1, "PICO W" },
  { IDX_PICO_TEMP, nullptr },
  { IDX_PICO_HUM, nullptr },
  { IDX_PICO_CO2, nullptr },
  { IDX_PICO_THI, nullptr },
  { -1, "OUTSIDE" },
  { IDX_OUT_TEMP, nullptr },
  { IDX_OUT_HUM, nullptr },
  { IDX_OUT_PRESS, nullptr },
  { -1, "SYSTEM" },
  { IDX_RPI_TEMP, nullptr },
  { IDX_QZSS_TEMP, nullptr },
  { IDX_STUDY_CO2, nullptr },
  { IDX_STUDY_TEMP, nullptr },
  { IDX_STUDY_HUM, nullptr },
  // { IDX_M5CAP_CLIENTS, nullptr }, // 非表示にする場合はコメントアウト
};
static constexpr int ROW_COUNT = sizeof(rows) / sizeof(rows[0]);

// Gauges (configurable via config.h; defaults here)
bool g_enable[SENSOR_COUNT];
float g_min[SENSOR_COUNT];
float g_max[SENSOR_COUNT];
#ifndef GAUGE_RAIN_CUR_ENABLE
#define GAUGE_RAIN_CUR_ENABLE 0
#define GAUGE_RAIN_CUR_MIN 0
#define GAUGE_RAIN_CUR_MAX 4095
#endif
#ifndef GAUGE_RAIN_BASE_ENABLE
#define GAUGE_RAIN_BASE_ENABLE 0
#define GAUGE_RAIN_BASE_MIN 0
#define GAUGE_RAIN_BASE_MAX 4095
#endif
#ifndef GAUGE_RAIN_UPTIME_ENABLE
#define GAUGE_RAIN_UPTIME_ENABLE 0
#define GAUGE_RAIN_UPTIME_MIN 0
#define GAUGE_RAIN_UPTIME_MAX 48
#endif
#ifndef GAUGE_PICO_TEMP_ENABLE
#define GAUGE_PICO_TEMP_ENABLE 1
#define GAUGE_PICO_TEMP_MIN -10
#define GAUGE_PICO_TEMP_MAX 45
#endif
#ifndef GAUGE_PICO_HUM_ENABLE
#define GAUGE_PICO_HUM_ENABLE 1
#define GAUGE_PICO_HUM_MIN 0
#define GAUGE_PICO_HUM_MAX 100
#endif
#ifndef GAUGE_PICO_CO2_ENABLE
#define GAUGE_PICO_CO2_ENABLE 1
#define GAUGE_PICO_CO2_MIN 400
#define GAUGE_PICO_CO2_MAX 2000
#endif
#ifndef GAUGE_PICO_THI_ENABLE
#define GAUGE_PICO_THI_ENABLE 1
#define GAUGE_PICO_THI_MIN 50
#define GAUGE_PICO_THI_MAX 85
#endif
#ifndef GAUGE_OUT_TEMP_ENABLE
#define GAUGE_OUT_TEMP_ENABLE 1
#define GAUGE_OUT_TEMP_MIN -10
#define GAUGE_OUT_TEMP_MAX 40
#endif
#ifndef GAUGE_OUT_HUM_ENABLE
#define GAUGE_OUT_HUM_ENABLE 1
#define GAUGE_OUT_HUM_MIN 0
#define GAUGE_OUT_HUM_MAX 100
#endif
#ifndef GAUGE_OUT_PRESS_ENABLE
#define GAUGE_OUT_PRESS_ENABLE 1
#define GAUGE_OUT_PRESS_MIN 980
#define GAUGE_OUT_PRESS_MAX 1040
#endif
#ifndef GAUGE_RPI_TEMP_ENABLE
#define GAUGE_RPI_TEMP_ENABLE 1
#define GAUGE_RPI_TEMP_MIN 20
#define GAUGE_RPI_TEMP_MAX 90
#endif
#ifndef GAUGE_QZSS_TEMP_ENABLE
#define GAUGE_QZSS_TEMP_ENABLE 1
#define GAUGE_QZSS_TEMP_MIN 20
#define GAUGE_QZSS_TEMP_MAX 90
#endif
#ifndef GAUGE_STUDY_CO2_ENABLE
#define GAUGE_STUDY_CO2_ENABLE 1
#define GAUGE_STUDY_CO2_MIN 400
#define GAUGE_STUDY_CO2_MAX 2000
#endif
#ifndef GAUGE_STUDY_TEMP_ENABLE
#define GAUGE_STUDY_TEMP_ENABLE 1
#define GAUGE_STUDY_TEMP_MIN 0
#define GAUGE_STUDY_TEMP_MAX 40
#endif
#ifndef GAUGE_STUDY_HUM_ENABLE
#define GAUGE_STUDY_HUM_ENABLE 1
#define GAUGE_STUDY_HUM_MIN 0
#define GAUGE_STUDY_HUM_MAX 100
#endif

// THI band
#ifndef THI_COOL_MAX
#define THI_COOL_MAX 65
#endif
#ifndef THI_COMFY_MAX
#define THI_COMFY_MAX 72
#endif
#ifndef THI_WARM_MAX
#define THI_WARM_MAX 78
#endif

// Time & stats
time_t g_lastMqttUpdate = 0;
// CO2 1h average ring buffer
#define CO2_BUF_CAP 120
time_t co2_t[CO2_BUF_CAP];
float co2_v[CO2_BUF_CAP];
int co2_head = 0, co2_count = 0;
void co2Push(time_t ts, float v) {
  co2_t[co2_head] = ts;
  co2_v[co2_head] = v;
  co2_head = (co2_head + 1) % CO2_BUF_CAP;
  if (co2_count < CO2_BUF_CAP) co2_count++;
}
float co2Avg1h() {
  time_t now = time(nullptr);
  if (now <= 0 || co2_count == 0) return NAN;
  float sum = 0;
  int n = 0;
  for (int i = 0; i < co2_count; ++i) {
    int idx = (co2_head - 1 - i + CO2_BUF_CAP) % CO2_BUF_CAP;
    if (now - co2_t[idx] <= 3600) {
      sum += co2_v[idx];
      ++n;
    } else break;
  }
  return n ? sum / n : NAN;
}
struct DailyStats {
  int yday = -1;
  bool inited = false;
  float outTempMin = 0, outTempMax = 0, outHumMax = 0;
} daily;
void dailyResetIfNeeded() {
  time_t now = time(nullptr);
  if (now <= 0) return;
  struct tm tmv;
  localtime_r(&now, &tmv);
  if (tmv.tm_yday != daily.yday) {
    daily.yday = tmv.tm_yday;
    daily.inited = false;
  }
}
void dailyUpdate(float tVal, bool hasT, float hVal, bool hasH) {
  dailyResetIfNeeded();
  if (!daily.inited) {
    daily.inited = true;
    daily.outTempMin = hasT ? tVal : 0;
    daily.outTempMax = hasT ? tVal : 0;
    daily.outHumMax = hasH ? hVal : 0;
  }
  if (hasT) {
    if (tVal < daily.outTempMin) daily.outTempMin = tVal;
    if (tVal > daily.outTempMax) daily.outTempMax = tVal;
  }
  if (hasH) {
    if (hVal > daily.outHumMax) daily.outHumMax = hVal;
  }
}

// ───────── Helpers ─────────
static float clamp01(float x) {
  if (x < 0) return 0;
  if (x > 1) return 1;
  return x;
}
void setGauge(uint8_t idx, bool en, float vmin, float vmax) {
  if (idx < SENSOR_COUNT) {
    g_enable[idx] = en;
    g_min[idx] = vmin;
    g_max[idx] = vmax;
  }
}
float gaugePercent(uint8_t idx, float v) {
  if (!g_enable[idx]) return 0.0f;
  float a = g_min[idx], b = g_max[idx];
  if (b <= a) return 0.0f;
  return clamp01((v - a) / (b - a));
}
float parseTempFromText(const char* s) {
  if (!s) return NAN;
  const char* p = s;
  while (*p && !((*p >= '0' && p[0] <= '9') || *p == '-')) ++p;
  return strtof(p, nullptr);
}
bool isNumericLike(const String& s) {
  if (!s.length()) return false;
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == ' ') continue;
    return false;
  }
  return true;
}
String formatValueForColumn2(const String& s, int maxW) {
  if (canvas.textWidth(s) <= maxW) return s;
  if (isNumericLike(s)) {
    double v = atof(s.c_str());
    return String(v, 2);
  }
  String t = s, ell = "...";
  int ellw = canvas.textWidth(ell);
  while (t.length() > 0 && canvas.textWidth(t) + ellw > maxW) t.remove(t.length() - 1);
  return t.length() ? t + ell : ell;
}
String formatDateTime(time_t ts) {
  if (ts <= 0) return String("--");
  struct tm tmv;
  if (!localtime_r(&ts, &tmv)) return String("--");
  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
  return String(buf);
}
inline bool isBinaryIndex(uint8_t idx) {
  return (idx == IDX_RAIN_STATE) || (idx == IDX_RAIN_CABLE);
}
bool binaryIsPositive(uint8_t idx) {
  const String& v = sensors[idx].value;
  if (idx == IDX_RAIN_STATE) return v == "ON";
  if (idx == IDX_RAIN_CABLE) return v == "OK";
  return false;
}
const char* thiLabel(float thi) {
  if (isnan(thi)) return "--";
  if (thi < THI_COOL_MAX) return "COOL";
  if (thi < THI_COMFY_MAX) return "COMFY";
  if (thi < THI_WARM_MAX) return "WARM";
  return "HOT";
}

// ───────── Rendering helpers ─────────
void drawGaugeBar(int x, int y, int w, int h, float percent) {
  canvas.drawRect(x, y, w, h, COLOR_FG);
  int fillW = (int)(percent * (w - 2));
  if (fillW <= 0) return;
  for (int yy = y + 1; yy < y + h - 1; ++yy) canvas.drawFastHLine(x + 1, yy, fillW, COLOR_LINE);
  for (int sx = x + 1; sx < x + 1 + fillW; sx += 3) canvas.drawFastVLine(sx, y + 1, h - 2, COLOR_FG);
}
void drawBinaryGauge(int x, int y, int w, int h, bool on) {
  canvas.drawRect(x, y, w, h, COLOR_FG);
  if (!on) return;
  for (int yy = y + 1; yy < y + h - 1; ++yy) {
    if (((yy - (y + 1)) % 4) != 3) canvas.drawFastHLine(x + 1, yy, w - 2, COLOR_FG);
  }
}

// total content height for given numbers (⚠ LayoutStyleを引数にしない)
int calcRowsHeight_ints(int lineH, int secExtra) {
  int h = 0;
  for (int i = 0; i < ROW_COUNT; ++i) {
    bool header = rows[i].header != nullptr;
    h += header ? (lineH + secExtra) : lineH;
  }
  return h;
}
void chooseStyleAndGlance(bool& showGlance) {
  const int H = M5.Display.height();
  const LayoutStyle CAND[3] = { STYLE_NORMAL, STYLE_COMPACT, STYLE_ULTRA };
  for (int s = 0; s < 3; ++s) {
    const LayoutStyle& S = CAND[s];
    int avail = H - S.HEADER_H - S.FOOTER_H - 12;
    int rowsH = calcRowsHeight_ints(S.LINE_H, S.SEC_EXTRA);
    if (rowsH <= avail) {
      showGlance = (rowsH + 4 + S.GLANCE_H) <= avail;
      L = S;
      return;
    }
  }
  L = STYLE_ULTRA;
  showGlance = false;
}

// ───────── Wi-Fi / MQTT ─────────
void setupWatchdog() {
#if ENABLE_WATCHDOG && defined(ARDUINO_ARCH_ESP32)
  // [FIX] "TWDT already initialized" エラーを回避するため、一度WDTを無効化してから再初期化します。
  // これにより、Arduinoコアによって既に初期化されている場合でも、意図した設定を確実に適用できます。
  esp_task_wdt_deinit();

  esp_task_wdt_config_t twdt_cfg = {};
  twdt_cfg.timeout_ms = WATCHDOG_TIMEOUT_SEC * 1000;
  twdt_cfg.trigger_panic = true;
  twdt_cfg.idle_core_mask = 0;
  esp_task_wdt_init(&twdt_cfg);

  esp_task_wdt_add(NULL);  // 現在のタスク（loopTask）を監視対象に追加
#endif
}


void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  uint32_t now = millis();
  if (now - wifiLastTry < wifiBackoff) return;
  wifiLastTry = now;

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(MQTT_CLIENT_ID);
  WiFi.disconnect(true, true);
  delay(50);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t st = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - st < 8000) {
    delay(100);
    yield();
// [FIX] Wi-Fi接続待機中にタイムアウトするのを防ぐため、ウォッチドッグをリセットします。
#if ENABLE_WATCHDOG && defined(ARDUINO_ARCH_ESP32)
    esp_task_wdt_reset();
#endif
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiBackoff = 1000;
  } else {
    wifiBackoff = cap_u32(wifiBackoff * 2, WIFI_MAX_BACKOFF_MS);
  }
}

void initTimeIfEnabled() {
#if ENABLE_NTP
  if (lastNtpSync == 0 || millis() - lastNtpSync > NTP_REFRESH_INTERVAL_MS) {
    configTzTime(TZ_INFO, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);
    struct tm tmv;
    for (int i = 0; i < 25; ++i) {
      if (getLocalTime(&tmv, 200)) break;
      delay(50);
    }
    lastNtpSync = millis();
  }
#endif
}

void mqttTune() {
  mqtt.setKeepAlive(20);
  mqtt.setSocketTimeout(5);
  mqtt.setBufferSize(1536);
}

void mqttSubscribeAll() {
  mqtt.subscribe(TOPIC_RAIN);
  mqtt.subscribe(TOPIC_PICO);
  mqtt.subscribe(TOPIC_ENV4);
  mqtt.subscribe(TOPIC_RPI_TEMP);
  mqtt.subscribe(TOPIC_QZSS_TEMP);
  mqtt.subscribe(TOPIC_M5STICKC);
  mqtt.subscribe(TOPIC_M5CAPSULE);
}

void mqttReconnect() {
  if (mqtt.connected()) return;
  uint32_t now = millis();
  if (now - mqttLastTry < mqttBackoff) return;
  mqttLastTry = now;

  ensureWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    mqttBackoff = cap_u32(mqttBackoff * 2, MQTT_MAX_BACKOFF_MS);
    return;
  }

  bool ok = strlen(MQTT_USER) ? mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)
                              : mqtt.connect(MQTT_CLIENT_ID);
  if (ok) {
    mqttBackoff = 1000;
    mqttSubscribeAll();
  } else {
    mqttBackoff = cap_u32(mqttBackoff * 2, MQTT_MAX_BACKOFF_MS);
  }
}

// ───────── MQTT Handler ─────────
void onMqttMessage(char* topic, byte* payload, uint16_t len) {
  String t(topic), js;
  js.reserve(len + 1);
  for (uint16_t i = 0; i < len; ++i) js += (char)payload[i];
  g_lastMqttUpdate = time(nullptr);

  if (t == TOPIC_RAIN) {
    StaticJsonDocument<512> d;
    if (!deserializeJson(d, js)) {
      sensors[IDX_RAIN_STATE].value = (d["rain"] | false) ? "ON" : "OFF";
      if (d.containsKey("current")) sensors[IDX_RAIN_CUR].value = String(d["current"].as<float>(), 1);
      if (d.containsKey("baseline")) sensors[IDX_RAIN_BASE].value = String(d["baseline"].as<float>(), 1);
      if (d.containsKey("uptime")) sensors[IDX_RAIN_UPTIME].value = String(d["uptime"].as<float>(), 2);
      if (d.containsKey("cable_ok")) sensors[IDX_RAIN_CABLE].value = d["cable_ok"].as<bool>() ? "OK" : "NG";
    }
  } else if (t == TOPIC_PICO) {
    StaticJsonDocument<512> d;
    if (!deserializeJson(d, js)) {
      if (d.containsKey("temperature")) sensors[IDX_PICO_TEMP].value = String(d["temperature"].as<float>(), 2);
      if (d.containsKey("humidity")) sensors[IDX_PICO_HUM].value = String(d["humidity"].as<float>(), 2);
      if (d.containsKey("co2")) {
        int v = d["co2"].as<int>();
        sensors[IDX_PICO_CO2].value = String(v);
        co2Push(time(nullptr), (float)v);
      }
      if (d.containsKey("thi")) sensors[IDX_PICO_THI].value = String(d["thi"].as<float>(), 1);
    }
  } else if (t == TOPIC_ENV4) {
    StaticJsonDocument<512> d;
    if (!deserializeJson(d, js)) {
      bool ht = false, hh = false;
      float tv = 0, hv = 0;
      if (d.containsKey("temperature")) {
        tv = d["temperature"].as<float>();
        sensors[IDX_OUT_TEMP].value = String(tv, 1);
        ht = true;
      }
      if (d.containsKey("humidity")) {
        hv = d["humidity"].as<float>();
        sensors[IDX_OUT_HUM].value = String(hv, 2);
        hh = true;
      }
      if (d.containsKey("pressure")) { sensors[IDX_OUT_PRESS].value = String(d["pressure"].as<float>(), 2); }
      if (ht || hh) dailyUpdate(tv, ht, hv, hh);
    }
  } else if (t == TOPIC_RPI_TEMP) {
    float v = parseTempFromText(js.c_str());
    if (!isnan(v)) sensors[IDX_RPI_TEMP].value = String(v, 1);
  } else if (t == TOPIC_QZSS_TEMP) {
    StaticJsonDocument<256> d;
    if (!deserializeJson(d, js)) {
      if (d.containsKey("temperature")) sensors[IDX_QZSS_TEMP].value = String(d["temperature"].as<float>(), 1);
    }
  } else if (t == TOPIC_M5STICKC) {
    StaticJsonDocument<512> d;
    if (!deserializeJson(d, js)) {
      if (d.containsKey("co2")) sensors[IDX_STUDY_CO2].value = String(d["co2"].as<int>());
      if (d.containsKey("temp")) sensors[IDX_STUDY_TEMP].value = String(d["temp"].as<float>(), 1);
      if (d.containsKey("hum")) sensors[IDX_STUDY_HUM].value = String(d["hum"].as<float>(), 2);
    }
  } else if (t == TOPIC_M5CAPSULE) {
    StaticJsonDocument<256> d;
    if (!deserializeJson(d, js)) {
      if (d.containsKey("client_count")) sensors[IDX_M5CAP_CLIENTS].value = String(d["client_count"].as<int>());
    }
  }
}

// ───────── Drawing ─────────
void drawHeader() {
  const int W = M5.Display.width();
  canvas.drawFastHLine(0, L.HEADER_H - 1, W, COLOR_LINE);

  // Title: CENTERED
  canvas.setTextColor(COLOR_FG, COLOR_BG);
  canvas.setFont(L.TITLE_FONT);
  canvas.setTextDatum(textdatum_t::top_center);
  canvas.drawString(String(DEVICE_TITLE), W / 2, 10);

  // Status: right aligned
  canvas.setFont(L.SMALL_FONT);
  canvas.setTextDatum(textdatum_t::top_left);
  String stat = String((WiFi.status() == WL_CONNECTED) ? "WiFi:OK" : "WiFi:NG");
  stat += "  ";
  stat += (mqtt.connected() ? "MQTT:OK" : "MQTT:NG");
  stat += "  ";
  stat += (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("-.-.-.-"));
  canvas.drawRightString(stat, W - RIGHT_PAD, 34);
}

void drawRow(int sensorIdx, int y) {
  const int W = M5.Display.width();
  canvas.setFont(L.LABEL_FONT);
  canvas.setTextColor(COLOR_DIM, COLOR_BG);
  canvas.setTextDatum(textdatum_t::top_left);
  canvas.drawString(sensors[sensorIdx].label, LEFT_PAD, y);

  const bool showBinary = isBinaryIndex(sensorIdx);
  const bool showNumericGauge = g_enable[sensorIdx];
  const bool showGauge = showBinary || showNumericGauge;

  canvas.setFont(L.VALUE_FONT);
  canvas.setTextColor(COLOR_FG, COLOR_BG);
  canvas.setTextDatum(textdatum_t::top_right);

  int maxW = showGauge ? L.VALUE_COL_W : (L.VALUE_COL_W + L.GAUGE_W + L.GAP_COL);
  int valueRightX = W - RIGHT_PAD - (showGauge ? (L.GAUGE_W + L.GAP_COL) : 0);

  String raw = sensors[sensorIdx].value.length() ? sensors[sensorIdx].value : "-";
  String disp = formatValueForColumn2(raw, maxW);
  canvas.drawString(disp, valueRightX, y);

  if (showGauge) {
    int gx = W - RIGHT_PAD - L.GAUGE_W;
    int gy = y + (L.LINE_H - L.GAUGE_H) / 2;
    if (showBinary) drawBinaryGauge(gx, gy, L.GAUGE_W, L.GAUGE_H, binaryIsPositive(sensorIdx));
    else drawGaugeBar(gx, gy, L.GAUGE_W, L.GAUGE_H, gaugePercent(sensorIdx, sensors[sensorIdx].value.toFloat()));
  }
}

void drawTodayCard(int yStart) {
  const int W = M5.Display.width();
  const int cardMargin = LEFT_PAD;
  const int cardW = W - LEFT_PAD - RIGHT_PAD;
  const int cardH = L.GLANCE_H;

  canvas.drawRect(cardMargin, yStart, cardW, cardH, COLOR_LINE);

  canvas.setTextColor(COLOR_DIM, COLOR_BG);
  canvas.setFont(L.SMALL_FONT);
  canvas.setTextDatum(textdatum_t::top_left);
  canvas.drawString("Today at a glance", cardMargin + 8, yStart + 6);

  int x = cardMargin + 14;
  int y = yStart + 22;

  float thi = sensors[IDX_PICO_THI].value.toFloat();
  canvas.drawString(String("THI: ") + (sensors[IDX_PICO_THI].value.length() ? sensors[IDX_PICO_THI].value : "-")
                      + " (" + thiLabel(thi) + ")",
                    x, y);

  y += 18;
  float co2_latest = sensors[IDX_PICO_CO2].value.toFloat();
  float co2_avg = co2Avg1h();
  String co2s = String("CO2: ") + (isnan(co2_latest) ? String("-") : String((int)co2_latest)) + " ppm";
  if (!isnan(co2_avg)) co2s += String("  /  1h avg ") + String((int)(co2_avg + 0.5f)) + " ppm";
  canvas.drawString(co2s, x, y);

  y += 18;
  dailyResetIfNeeded();
  String outs = "Outside: ";
  if (daily.inited) {
    outs += "Hi " + String(daily.outTempMax, 1) + " / Lo " + String(daily.outTempMin, 1) + " degC";
    outs += "   Hum max " + String(daily.outHumMax, 1) + "%";
  } else outs += "--";
  canvas.drawString(outs, x, y);

  y += 18;
  String pres = String("Pressure: ") + (sensors[IDX_OUT_PRESS].value.length() ? sensors[IDX_OUT_PRESS].value + " hPa" : "-");
  canvas.drawString(pres, x, y);

  canvas.setTextColor(COLOR_DIM, COLOR_BG);
  canvas.setTextDatum(textdatum_t::bottom_right);
  long rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  size_t freeHeap = ESP.getFreeHeap() / 1024;
  String sys = String("Wi-Fi ") + (WiFi.status() == WL_CONNECTED ? String(rssi) + " dBm" : String("NG"))
               + "   Heap " + String(freeHeap) + " kB";
  canvas.drawString(sys, cardMargin + cardW - 8, yStart + cardH - 6);
}

void drawFooter() {
  const int W = M5.Display.width(), H = M5.Display.height();
  canvas.drawFastHLine(0, H - L.FOOTER_H, W, COLOR_LINE);
  canvas.setFont(L.SMALL_FONT);
  int fh = canvas.fontHeight();
  int topY = H - L.FOOTER_H + 6;
  int line2Y = topY + fh + 4;

  canvas.setTextDatum(textdatum_t::top_center);
  canvas.drawString("TapTop:Refresh", W / 2, topY);

  String mqttInfo = String("Last MQTT update: ") + formatDateTime(g_lastMqttUpdate);
  canvas.drawString(mqttInfo, W / 2, line2Y);
}

void drawDashboard() {
  bool showGlance;
  chooseStyleAndGlance(showGlance);

  const int W = M5.Display.width(), H = M5.Display.height();
  canvas.setColorDepth(1);
  canvas.createSprite(W, H);
  canvas.fillScreen(COLOR_BG);

  drawHeader();

  int y = L.HEADER_H + 6;
  const int bottom = H - L.FOOTER_H - 6;

  for (int i = 0; i < ROW_COUNT; ++i) {
    bool isHdr = rows[i].header != nullptr;
    int rh = isHdr ? (L.LINE_H + L.SEC_EXTRA) : L.LINE_H;
    if (y + rh > bottom) break;

    if (isHdr) {
      if (i != 0) y += 6;
      canvas.setTextColor(COLOR_DIM, COLOR_BG);
      canvas.setFont(L.SEC_FONT);
      canvas.setTextDatum(textdatum_t::top_left);
      canvas.drawString(rows[i].header, LEFT_PAD, y);
      y += rh;
    } else {
      if (i != 0) canvas.drawFastHLine(LEFT_PAD, y - 6, W - LEFT_PAD - RIGHT_PAD, COLOR_LINE);
      drawRow(rows[i].sensor, y);
      y += L.LINE_H;
    }
  }

  if (showGlance && y + 4 + L.GLANCE_H <= bottom) {
    drawTodayCard(y + 4);
  }

  drawFooter();

  canvas.pushSprite(0, 0);

// [FIX] E-Inkディスプレイの更新は数秒かかるため、ウォッチドッグが作動するのを防ぎます。
#if ENABLE_WATCHDOG && defined(ARDUINO_ARCH_ESP32)
  esp_task_wdt_reset();
#endif

  M5.Display.display();
  M5.Display.waitDisplay();
  canvas.deleteSprite();
}

// ───────── Touch (tap top to refresh) ─────────
void handleTouch() {
  if (!M5.Touch.isEnabled()) return;
  static uint32_t lastTap = 0;
  auto t = M5.Touch.getDetail();
  if (!t.isPressed()) return;
  if (millis() - lastTap < 250) return;  // debounce
  lastTap = millis();
  if (t.y < L.HEADER_H) drawDashboard();
}

// ───────── Setup / Loop ─────────
uint32_t lastRefresh = 0;

void applyGaugeFromConfig() {
  setGauge(IDX_RAIN_CUR, GAUGE_RAIN_CUR_ENABLE, GAUGE_RAIN_CUR_MIN, GAUGE_RAIN_CUR_MAX);
  setGauge(IDX_RAIN_BASE, GAUGE_RAIN_BASE_ENABLE, GAUGE_RAIN_BASE_MIN, GAUGE_RAIN_BASE_MAX);
  setGauge(IDX_RAIN_UPTIME, GAUGE_RAIN_UPTIME_ENABLE, GAUGE_RAIN_UPTIME_MIN, GAUGE_RAIN_UPTIME_MAX);

  setGauge(IDX_PICO_TEMP, GAUGE_PICO_TEMP_ENABLE, GAUGE_PICO_TEMP_MIN, GAUGE_PICO_TEMP_MAX);
  setGauge(IDX_PICO_HUM, GAUGE_PICO_HUM_ENABLE, GAUGE_PICO_HUM_MIN, GAUGE_PICO_HUM_MAX);
  setGauge(IDX_PICO_CO2, GAUGE_PICO_CO2_ENABLE, GAUGE_PICO_CO2_MIN, GAUGE_PICO_CO2_MAX);
  setGauge(IDX_PICO_THI, GAUGE_PICO_THI_ENABLE, GAUGE_PICO_THI_MIN, GAUGE_PICO_THI_MAX);

  setGauge(IDX_OUT_TEMP, GAUGE_OUT_TEMP_ENABLE, GAUGE_OUT_TEMP_MIN, GAUGE_OUT_TEMP_MAX);
  setGauge(IDX_OUT_HUM, GAUGE_OUT_HUM_ENABLE, GAUGE_OUT_HUM_MIN, GAUGE_OUT_HUM_MAX);
  setGauge(IDX_OUT_PRESS, GAUGE_OUT_PRESS_ENABLE, GAUGE_OUT_PRESS_MIN, GAUGE_OUT_PRESS_MAX);

  setGauge(IDX_RPI_TEMP, GAUGE_RPI_TEMP_ENABLE, GAUGE_RPI_TEMP_MIN, GAUGE_RPI_TEMP_MAX);
  setGauge(IDX_QZSS_TEMP, GAUGE_QZSS_TEMP_ENABLE, GAUGE_QZSS_TEMP_MIN, GAUGE_QZSS_TEMP_MAX);

  setGauge(IDX_STUDY_CO2, GAUGE_STUDY_CO2_ENABLE, GAUGE_STUDY_CO2_MIN, GAUGE_STUDY_CO2_MAX);
  setGauge(IDX_STUDY_TEMP, GAUGE_STUDY_TEMP_ENABLE, GAUGE_STUDY_TEMP_MIN, GAUGE_STUDY_TEMP_MAX);
  setGauge(IDX_STUDY_HUM, GAUGE_STUDY_HUM_ENABLE, GAUGE_STUDY_HUM_MIN, GAUGE_STUDY_HUM_MAX);
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(0);

  setupWatchdog();

  // Boot splash (ASCII only)
  canvas.setColorDepth(1);
  canvas.createSprite(M5.Display.width(), M5.Display.height());
  canvas.fillScreen(COLOR_BG);
  canvas.setTextColor(COLOR_FG, COLOR_BG);
  canvas.setFont(&fonts::Font4);
  canvas.setTextDatum(textdatum_t::top_left);
  canvas.drawString("Booting...", 16, 16);
  canvas.setFont(&fonts::Font2);
  canvas.drawString("Connecting Wi-Fi", 16, 56);
  canvas.pushSprite(0, 0);
  M5.Display.display();
  M5.Display.waitDisplay();
  canvas.deleteSprite();

  for (int i = 0; i < SENSOR_COUNT; ++i) {
    g_enable[i] = false;
    g_min[i] = 0;
    g_max[i] = 1;
  }
  applyGaugeFromConfig();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqttTune();

  ensureWiFi();
  initTimeIfEnabled();
  mqttReconnect();

  drawDashboard();
  lastRefresh = millis();
  lastHealth = millis();
}

void loop() {
#if ENABLE_WATCHDOG && defined(ARDUINO_ARCH_ESP32)
  esp_task_wdt_reset();
#endif

  ensureWiFi();
  mqttReconnect();
  if (mqtt.connected()) mqtt.loop();

  handleTouch();

  if (millis() - lastRefresh > (REFRESH_SEC * 1000UL)) {
    drawDashboard();
    lastRefresh = millis();
  }

  if (millis() - lastHealth > HEALTH_CHECK_INTERVAL_MS) {
    lastHealth = millis();
    size_t freeKB = ESP.getFreeHeap() / 1024;
    if (freeKB < LOW_HEAP_RESTART_KB) {
      delay(50);
      ESP.restart();
    }
    initTimeIfEnabled();
  }

  delay(5);
}
