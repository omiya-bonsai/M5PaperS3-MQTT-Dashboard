// /*
//  * M5PaperS3 MQTT Dashboard — リーダブル版（学習用にコメントを大幅追加）
//  * ------------------------------------------------------------------
//  * このスケッチは、MQTTで受け取った各種センサー値をM5PaperS3の電子ペーパーに
//  * ダッシュボードとして描画します。NTPで時刻同期し、値の更新が止まった場合の
//  * 監視（STALE検出）、季節ごとのゲージ範囲（任意）、ウォッチドッグによるハング
//  * 防止などを備えています。
//  *
//  * ▼最初にやること
//  *   1) 同梱の config.example.readable.h を config.h にリネームして、Wi‑Fiや
//  *      MQTTの設定を自分の環境に合わせて書き換えます（SSID/パスワード等）。
//  *   2) ボードマネージャ＆ライブラリ
//  *        - M5Unified 0.2.x
//  *        - M5GFX     0.2.x
//  *        - PubSubClient
//  *        - ArduinoJson 7.x
//  *   3) ビルド＆書き込み後、MQTTブローカーに接続できれば各ゲージが自動で更新されます。
//  *
//  * ▼ファイル構成（ざっくりの読み順）
//  *   - #include と 定数/型定義：必要なライブラリと全体設定
//  *   - ユーティリティ関数群：文字列整形・値監視・描画補助など
//  *   - MQTT関連：接続・再接続・購読・受信ハンドラ
//  *   - 描画関連：ヘッダー/行/カード/フッター/全体描画
//  *   - setup()/loop()：初期化とメインループ
//  *
//  * ▼注意（公開リポジトリ）
//  *   - config.h 内のSSID/パスワードなどの秘密情報は、必ずダミーに置き換えた
//  *     config.example.h を公開し、手元では config.h を使う運用にしてください。
//  *
//  * このコメントブロック以下に、各関数へ「役割とポイント」を簡潔に書いた
//  * ドキュメンテーションコメントを自動付与しています。* 関数名や処理内容から推測した説明なので、必要に応じて微調整してください。
//  * /


#include <M5Unified.h>
#include <M5GFX.h>
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


// 下も含めてコメントアウトをしないと
// ダメっぽい
// #dt:2025-08-28 #tm:16:02
// #ifndef ENABLE_SEASONAL_GAUGES
// #define ENABLE_SEASONAL_GAUGES 1
// #endif

// config.h で定義をしているからコメントアウト
// #dt:2025-08-28 #tm:15:33
// config.h の設定を読めていないようなので、
// ここで直接、四季の始まりの月日を指定する
// #dt:2025-08-28 #tm:15:38
#ifndef SEASON_SPRING_START_MMDD
#define SEASON_SPRING_START_MMDD 304
#endif
#ifndef SEASON_SUMMER_START_MMDD
#define SEASON_SUMMER_START_MMDD 505
#endif
#ifndef SEASON_AUTUMN_START_MMDD
#define SEASON_AUTUMN_START_MMDD 1007
#endif
#ifndef SEASON_WINTER_START_MMDD
#define SEASON_WINTER_START_MMDD 1217
#endif

  enum Season : uint8_t { SEASON_SPRING,
                          SEASON_SUMMER,
                          SEASON_AUTUMN,
                          SEASON_WINTER };
/**
 * @brief 季節の列挙値から、人間向けラベル（英語表記）を返します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
static inline const char* seasonLabelJP(Season s) {
  /**
 * @brief switch の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
  switch (s) {
    case SEASON_SPRING: return "Spring";
    case SEASON_SUMMER: return "Summer";
    case SEASON_AUTUMN: return "Autumn";
    default: return "Winter";
  }
}
/**
 * @brief ローカル日付（mmdd）から四季のどれかを推定します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
static Season seasonFromLocalDate() {
  time_t now = time(nullptr);
  struct tm tmv;
  if (now <= 0 || !localtime_r(&now, &tmv)) return SEASON_SPRING;
  int mm = tmv.tm_mon + 1;
  int dd = tmv.tm_mday;
  int mmdd = mm * 100 + dd;
  if (mmdd >= SEASON_WINTER_START_MMDD || mmdd <= 304) return SEASON_WINTER;
  if (mmdd >= SEASON_SPRING_START_MMDD && mmdd <= 505) return SEASON_SPRING;
  if (mmdd >= SEASON_SUMMER_START_MMDD && mmdd <= 1007) return SEASON_SUMMER;
  return SEASON_AUTUMN;
}
static Season g_currentSeason = SEASON_SPRING;
static uint32_t g_lastSeasonCheckMs = 0;
void applySeasonalOverrides(Season s);

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
#define NTP_REFRESH_INTERVAL_MS 21600000UL
#endif
#ifndef STALE_MINUTES
#define STALE_MINUTES 15
#endif
static const long STALE_SEC = (long)STALE_MINUTES * 60L;

M5Canvas canvas(&M5.Display);
static constexpr uint32_t COLOR_BG = 0xFFFFFFu;
static constexpr uint32_t COLOR_FG = 0x000000u;
static constexpr uint32_t COLOR_DIM = 0x666666u;
static constexpr uint32_t COLOR_LINE = 0xDDDDDDu;
static constexpr int LEFT_PAD = 16;
static constexpr int RIGHT_PAD = 16;
static constexpr int COL_LABEL_RATIO = 30;
static constexpr int COL_VALUE_RATIO = 15;
static constexpr int COL_GAUGE_RATIO = 55;
/**
 * @brief uint32値の加算でオーバーフローしないよう上限に丸めます。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
static inline uint32_t cap_u32(uint32_t v, uint32_t vmax) {
  return (v > vmax) ? vmax : v;
}

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
static const LayoutStyle STYLE_NORMAL = { 64, 56, 38, 12, 160, 18, 100, 10, 112, &fonts::lgfxJapanGothic_24, &fonts::lgfxJapanGothic_20, &fonts::lgfxJapanGothic_24, &fonts::Font4, &fonts::Font2 };
static const LayoutStyle STYLE_COMPACT = { 58, 54, 34, 8, 150, 16, 96, 8, 100, &fonts::lgfxJapanGothic_24, &fonts::lgfxJapanGothic_16, &fonts::lgfxJapanGothic_20, &fonts::Font4, &fonts::Font2 };
static const LayoutStyle STYLE_ULTRA = { 54, 52, 30, 6, 144, 14, 92, 8, 90, &fonts::lgfxJapanGothic_20, &fonts::lgfxJapanGothic_16, &fonts::lgfxJapanGothic_16, &fonts::Font2, &fonts::Font2 };
LayoutStyle L = STYLE_NORMAL;

struct ColLayout {
  int labelW;
  int valueW;
  int gaugeW;
  int gap;
};
struct ColHelper {
  /**
 * @brief 汎用の計算・集計を行います（処理の詳細は関数本体参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
  static ColLayout compute() {
    ColLayout C;
    int inner = M5.Display.width() - LEFT_PAD - RIGHT_PAD;
    int gapTotal = 2 * L.GAP_COL;
    int avail = inner - gapTotal;
    if (avail < 10) avail = 10;
    int totalRatio = COL_LABEL_RATIO + COL_VALUE_RATIO + COL_GAUGE_RATIO;
    C.labelW = (avail * COL_LABEL_RATIO) / totalRatio;
    C.valueW = (avail * COL_VALUE_RATIO) / totalRatio;
    C.gaugeW = avail - C.labelW - C.valueW;
    C.gap = L.GAP_COL;
    return C;
  }
};

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
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
  { "雨", "-" },
  { "雨 現在値(ADC)", "-" },
  { "雨 ベースライン", "-" },
  { "雨 稼働時間(h)", "-" },
  { "雨 ケーブル", "-" },
  { "Pico 温度(°C)", "-" },
  { "Pico 湿度(%)", "-" },
  { "リビング CO2(ppm)", "-" },
  { "Pico THI", "-" },
  { "外 温度(°C)", "-" },
  { "外 湿度(%)", "-" },
  { "外 気圧(hPa)", "-" },
  { "RPi5 CPU(°C)", "-" },
  { "QZSS CPU(°C)", "-" },
  { "書斎 CO2(ppm)", "-" },
  { "書斎 温度(°C)", "-" },
  { "書斎 湿度(%)", "-" },
  { "M5Capsule Clients", "-" }
};

#ifndef STALE_EXCLUDE_LIST
#define STALE_EXCLUDE_LIST \
  { IDX_RAIN_STATE, IDX_RAIN_CABLE }
#endif
static const uint8_t kStaleExclude[] = STALE_EXCLUDE_LIST;
/**
 * @brief 無変動監視から除外すべきセンサーかを判定します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
static inline bool isStaleExcluded(uint8_t idx) {
  for (size_t i = 0; i < sizeof(kStaleExclude) / sizeof(kStaleExclude[0]); ++i) {
    if (kStaleExclude[i] == idx) return true;
  }
  return false;
}

String g_lastValue[SENSOR_COUNT];
time_t g_lastChangeTs[SENSOR_COUNT];
time_t g_lastUpdateTs[SENSOR_COUNT];
time_t g_errorFirstShownTs[SENSOR_COUNT];

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
  { IDX_STUDY_HUM, nullptr }
};
static constexpr int ROW_COUNT = sizeof(rows) / sizeof(rows[0]);

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

#ifndef THI_COOL_MAX
#define THI_COOL_MAX 65
#endif
#ifndef THI_COMFY_MAX
#define THI_COMFY_MAX 72
#endif
#ifndef THI_WARM_MAX
#define THI_WARM_MAX 78
#endif

time_t g_lastMqttUpdate = 0;
#define CO2_BUF_CAP 120
time_t co2_t[CO2_BUF_CAP];
float co2_v[CO2_BUF_CAP];
int co2_head = 0, co2_count = 0;
/**
 * @brief CO₂の新規サンプルをリングバッファへ蓄積します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
void co2Push(time_t ts, float v) {
  co2_t[co2_head] = ts;
  co2_v[co2_head] = v;
  co2_head = (co2_head + 1) % CO2_BUF_CAP;
  if (co2_count < CO2_BUF_CAP) co2_count++;
}
/**
 * @brief 直近1時間のCO₂平均値を計算して返します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
float co2Avg1h() {
  time_t now = time(nullptr);
  if (now <= 0 || co2_count == 0) return NAN;
  float sum = 0;
  int n = 0;
  for (int i = 0; i < co2_count; ++i) {
    int idx = (co2_head - 1 - i + CO2_BUF_CAP) % CO2_BUF_CAP;
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
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
/**
 * @brief 日付が変わったタイミングで日次リセットを行います。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
void dailyResetIfNeeded() {
  time_t now = time(nullptr);
  if (now <= 0) return;
  struct tm tmv;
  localtime_r(&now, &tmv);
  /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
  if (tmv.tm_yday != daily.yday) {
    daily.yday = tmv.tm_yday;
    daily.inited = false;
  }
}
/**
 * @brief 日次更新処理（統計や画面更新のきっかけ）を行います。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
void dailyUpdate(float tVal, bool hasT, float hVal, bool hasH) {
  dailyResetIfNeeded();
  /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
  if (!daily.inited) {
    daily.inited = true;
    daily.outTempMin = hasT ? tVal : 0;
    daily.outTempMax = hasT ? tVal : 0;
    daily.outHumMax = hasH ? hVal : 0;
  }
  /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
  if (hasT) {
    if (tVal < daily.outTempMin) daily.outTempMin = tVal;
    if (tVal > daily.outTempMax) daily.outTempMax = tVal;
  }
  /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
  if (hasH) {
    if (hVal > daily.outHumMax) daily.outHumMax = hVal;
  }
}

/**
 * @brief 0.0〜1.0の範囲に値をクランプします。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */

static float clamp01(float x) {
  if (x < 0) return 0;
  if (x > 1) return 1;
  return x;
}
/**
 * @brief ゲージ用の内部値を更新します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
void setGauge(uint8_t idx, bool en, float vmin, float vmax) {
  /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
  if (idx < SENSOR_COUNT) {
    g_enable[idx] = en;
    g_min[idx] = vmin;
    g_max[idx] = vmax;
  }
}
/**
 * @brief ゲージの現在値を0〜100%に正規化して返します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
float gaugePercent(uint8_t idx, float v) {
  if (!g_enable[idx]) return 0.0f;
  float a = g_min[idx], b = g_max[idx];
  if (b <= a) return 0.0f;
  return clamp01((v - a) / (b - a));
}
/**
 * @brief テキストから温度らしき数値を抽出します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
float parseTempFromText(const char* s) {
  if (!s) return NAN;
  const char* p = s;
  while (*p && !((*p >= '0' && *p <= '9') || *p == '-')) ++p;
  return strtof(p, nullptr);
}
/**
 * @brief 値が数値らしいか（数値/数値文字列か）を判定します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
bool isNumericLike(const String& s) {
  if (!s.length()) return false;
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == ' ') continue;
    return false;
  }
  return true;
}
/**
 * @brief 右側の値カラムに収まるように文字列を整形します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
String formatValueForColumn2(const String& s, int maxW) {
  if (canvas.textWidth(s) <= maxW) return s;
  /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
  if (isNumericLike(s)) {
    double v = atof(s.c_str());
    return String(v, 2);
  }
  String t = s, ell = "...";
  int ellw = canvas.textWidth(ell);
  while (t.length() > 0 && canvas.textWidth(t) + ellw > maxW) t.remove(t.length() - 1);
  return t.length() ? t + ell : ell;
}
/**
 * @brief 指定幅に収まらない文字列を中点省略などで短縮します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
String ellipsizeToWidth(const String& s, int maxW) {
  if (canvas.textWidth(s) <= maxW) return s;
  String t = s, ell = "...";
  int ellw = canvas.textWidth(ell);
  while (t.length() > 0 && canvas.textWidth(t) + ellw > maxW) t.remove(t.length() - 1);
  return t.length() ? t + ell : ell;
}
/**
 * @brief 時刻を YYYY-MM-DD HH:MM 形式の文字列に整形します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
String formatDateTime(time_t ts) {
  if (ts <= 0) return String("--");
  struct tm tmv;
  if (!localtime_r(&ts, &tmv)) return String("--");
  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
  return String(buf);
}
/**
 * @brief バイナリ（ON/OFF系）のゲージかどうかを判定します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
inline bool isBinaryIndex(uint8_t idx) {
  return (idx == IDX_RAIN_STATE) || (idx == IDX_RAIN_CABLE);
}
/**
 * @brief バイナリ値がポジティブ（良い状態）かどうかを判定します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
bool binaryIsPositive(uint8_t idx) {
  const String& v = sensors[idx].value;
  if (idx == IDX_RAIN_STATE) return v == "RAIN";
  if (idx == IDX_RAIN_CABLE) return v == "NG";
  return false;
}
/**
 * @brief THI（不快指数）に応じてラベルを返します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
const char* thiLabel(float thi) {
  if (isnan(thi)) return "--";
  if (thi < THI_COOL_MAX) return "COOL";
  if (thi < THI_COMFY_MAX) return "COMFY";
  if (thi < THI_WARM_MAX) return "WARM";
  return "HOT";
}

/**
 * @brief 無変動監視（STALE）判定用の配列を初期化します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */

void initStaleArrays() {
  for (int i = 0; i < SENSOR_COUNT; ++i) {
    g_lastValue[i] = "";
    g_lastChangeTs[i] = 0;
    g_lastUpdateTs[i] = 0;
    g_errorFirstShownTs[i] = 0;
  }
}
/**
 * @brief 任意センサーの最新値を受け取り、内部状態を更新します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
void pushSensorValue(uint8_t idx, const String& v) {
  if (idx >= SENSOR_COUNT) return;
  time_t now = time(nullptr);
  /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
  if (g_lastValue[idx].length() == 0) {
    g_lastChangeTs[idx] = now;
  } else if (v != g_lastValue[idx]) {
    g_lastChangeTs[idx] = now;
    g_errorFirstShownTs[idx] = 0;
  }
  g_lastValue[idx] = v;
  g_lastUpdateTs[idx] = now;
  sensors[idx].value = v;
}
/**
 * @brief 指定センサーが一定時間更新されていないかを判定します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
bool sensorIsStale(uint8_t idx, time_t now, time_t& outErrorShown, time_t& outInfoTime) {
  if (idx >= SENSOR_COUNT) return false;
  if (isStaleExcluded(idx)) return false;
  if (g_lastChangeTs[idx] == 0) return false;
  long sinceChange = (long)(now - g_lastChangeTs[idx]);
  /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
  if (sinceChange >= STALE_SEC) {
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (g_errorFirstShownTs[idx] == 0) { g_errorFirstShownTs[idx] = now; }
    outErrorShown = g_errorFirstShownTs[idx];
    outInfoTime = g_lastChangeTs[idx];
    return true;
  } else {
    g_errorFirstShownTs[idx] = 0;
    return false;
  }
}

/**
 * @brief 連続値のゲージ（棒）を描画します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */

void drawGaugeBar(int x, int y, int w, int h, float percent) {
  canvas.drawRect(x, y, w, h, COLOR_FG);
  int fillW = (int)(percent * (w - 2));
  if (fillW <= 0) return;
  for (int yy = y + 1; yy < y + h - 1; ++yy) canvas.drawFastHLine(x + 1, yy, fillW, COLOR_LINE);
  for (int sx = x + 1; sx < x + 1 + fillW; sx += 3) canvas.drawFastVLine(sx, y + 1, h - 2, COLOR_FG);
}
/**
 * @brief バイナリゲージ（ON/OFF）を描画します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
void drawBinaryGauge(int x, int y, int w, int h, bool on) {
  canvas.drawRect(x, y, w, h, COLOR_FG);
  if (!on) return;
  for (int yy = y + 1; yy < y + h - 1; ++yy) {
    if (((yy - (y + 1)) % 4) != 3) canvas.drawFastHLine(x + 1, yy, w - 2, COLOR_FG);
  }
}
/**
 * @brief 行ごとの高さと全体レイアウトを計算します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
int calcRowsHeight_ints(int lineH, int secExtra) {
  int h = 0;
  for (int i = 0; i < ROW_COUNT; ++i) {
    bool header = rows[i].header != nullptr;
    h += header ? (lineH + secExtra) : lineH;
  }
  return h;
}
/**
 * @brief 表示密度（ノーマル/コンパクト/ウルトラ）等を決定します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
void chooseStyleAndGlance(bool& showGlance) {
  const int H = M5.Display.height();
  const LayoutStyle CAND[3] = { STYLE_NORMAL, STYLE_COMPACT, STYLE_ULTRA };
  for (int s = 0; s < 3; ++s) {
    const LayoutStyle& S = CAND[s];
    int avail = H - S.HEADER_H - S.FOOTER_H - 12;
    int rowsH = calcRowsHeight_ints(S.LINE_H, S.SEC_EXTRA);
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (rowsH <= avail) {
      showGlance = (rowsH + 4 + S.GLANCE_H) <= avail;
      L = S;
      return;
    }
  }
  L = STYLE_ULTRA;
  showGlance = false;
}

/**
 * @brief ウォッチドッグタイマを設定します（ハング防止）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */

void setupWatchdog() {
#if ENABLE_WATCHDOG && defined(ARDUINO_ARCH_ESP32)
  esp_task_wdt_deinit();
  esp_task_wdt_config_t twdt_cfg = {};
  twdt_cfg.timeout_ms = WATCHDOG_TIMEOUT_SEC * 1000;
  twdt_cfg.trigger_panic = true;
  twdt_cfg.idle_core_mask = 0;
  esp_task_wdt_init(&twdt_cfg);
  esp_task_wdt_add(NULL);
#endif
}

/**
 * @brief Wi‑Fi接続を（指数バックオフで）確立/維持します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */

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
  /**
 * @brief while の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
  while (WiFi.status() != WL_CONNECTED && millis() - st < 8000) {
    delay(100);
    yield();
#if ENABLE_WATCHDOG && defined(ARDUINO_ARCH_ESP32)
    esp_task_wdt_reset();
#endif
  }
  if (WiFi.status() == WL_CONNECTED) wifiBackoff = 1000;
  else wifiBackoff = cap_u32(wifiBackoff * 2, WIFI_MAX_BACKOFF_MS);
}

/**
 * @brief NTPが有効なら時刻同期を初期化します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */

void initTimeIfEnabled() {
#if ENABLE_NTP
  /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
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

/**
 * @brief MQTTクライアントの各種タイムアウト等を調整します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */

void mqttTune() {
  mqtt.setKeepAlive(20);
  mqtt.setSocketTimeout(5);
  mqtt.setBufferSize(1536);
}
/**
 * @brief 利用する全トピックを購読します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
void mqttSubscribeAll() {
  mqtt.subscribe(TOPIC_RAIN);
  mqtt.subscribe(TOPIC_PICO);
  mqtt.subscribe(TOPIC_ENV4);
  mqtt.subscribe(TOPIC_RPI_TEMP);
  mqtt.subscribe(TOPIC_QZSS_TEMP);
  mqtt.subscribe(TOPIC_M5STICKC);
  mqtt.subscribe(TOPIC_M5CAPSULE);
}
/**
 * @brief MQTTブローカーに再接続します（必要なら再購読）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
void mqttReconnect() {
  if (mqtt.connected()) return;
  uint32_t now = millis();
  if (now - mqttLastTry < mqttBackoff) return;
  mqttLastTry = now;
  ensureWiFi();
  /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
  if (WiFi.status() != WL_CONNECTED) {
    mqttBackoff = cap_u32(mqttBackoff * 2, MQTT_MAX_BACKOFF_MS);
    return;
  }
  bool ok = strlen(MQTT_USER) ? mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS) : mqtt.connect(MQTT_CLIENT_ID);
  /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
  if (ok) {
    mqttBackoff = 1000;
    mqttSubscribeAll();
  } else {
    mqttBackoff = cap_u32(mqttBackoff * 2, MQTT_MAX_BACKOFF_MS);
  }
}

/**
 * @brief MQTTメッセージ受信時の処理（JSONパース→内部更新→再描画）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */

void onMqttMessage(char* topic, byte* payload, uint16_t len) {
  String t(topic), js;
  js.reserve(len + 1);
  for (uint16_t i = 0; i < len; ++i) js += (char)payload[i];
  g_lastMqttUpdate = time(nullptr);
  /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
  if (t == TOPIC_RAIN) {
    StaticJsonDocument<512> d;
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (!deserializeJson(d, js)) {
      pushSensorValue(IDX_RAIN_STATE, (d["rain"] | false) ? String("RAIN") : String("DRY"));
      if (d["current"].is<float>()) pushSensorValue(IDX_RAIN_CUR, String(d["current"].as<float>(), 1));
      if (d["baseline"].is<float>()) pushSensorValue(IDX_RAIN_BASE, String(d["baseline"].as<float>(), 1));
      if (d["uptime"].is<float>()) pushSensorValue(IDX_RAIN_UPTIME, String(d["uptime"].as<float>(), 2));
      if (d["cable_ok"].is<bool>()) pushSensorValue(IDX_RAIN_CABLE, d["cable_ok"].as<bool>() ? String("OK") : String("NG"));
    }
  } else if (t == TOPIC_PICO) {
    StaticJsonDocument<512> d;
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (!deserializeJson(d, js)) {
      if (d["temperature"].is<float>()) pushSensorValue(IDX_PICO_TEMP, String(d["temperature"].as<float>(), 2));
      if (d["humidity"].is<float>()) pushSensorValue(IDX_PICO_HUM, String(d["humidity"].as<float>(), 2));
      /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
      if (d["co2"].is<int>()) {
        int v = d["co2"].as<int>();
        pushSensorValue(IDX_PICO_CO2, String(v));
        co2Push(time(nullptr), (float)v);
      }
      if (d["thi"].is<float>()) pushSensorValue(IDX_PICO_THI, String(d["thi"].as<float>(), 1));
    }
  } else if (t == TOPIC_ENV4) {
    StaticJsonDocument<512> d;
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (!deserializeJson(d, js)) {
      bool ht = false, hh = false;
      float tv = 0, hv = 0;
      /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
      if (d["temperature"].is<float>()) {
        tv = d["temperature"].as<float>();
        pushSensorValue(IDX_OUT_TEMP, String(tv, 1));
        ht = true;
      }
      /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
      if (d["humidity"].is<float>()) {
        hv = d["humidity"].as<float>();
        pushSensorValue(IDX_OUT_HUM, String(hv, 2));
        hh = true;
      }
      /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
      if (d["pressure"].is<float>()) { pushSensorValue(IDX_OUT_PRESS, String(d["pressure"].as<float>(), 2)); }
      if (ht || hh) dailyUpdate(tv, ht, hv, hh);
    }
  } else if (t == TOPIC_RPI_TEMP) {
    float v = parseTempFromText(js.c_str());
    if (!isnan(v)) pushSensorValue(IDX_RPI_TEMP, String(v, 1));
  } else if (t == TOPIC_QZSS_TEMP) {
    StaticJsonDocument<256> d;
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (!deserializeJson(d, js)) {
      if (d["temperature"].is<float>()) pushSensorValue(IDX_QZSS_TEMP, String(d["temperature"].as<float>(), 1));
    }
  } else if (t == TOPIC_M5STICKC) {
    StaticJsonDocument<512> d;
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (!deserializeJson(d, js)) {
      if (d["co2"].is<int>()) pushSensorValue(IDX_STUDY_CO2, String(d["co2"].as<int>()));
      if (d["temp"].is<float>()) pushSensorValue(IDX_STUDY_TEMP, String(d["temp"].as<float>(), 1));
      if (d["hum"].is<float>()) pushSensorValue(IDX_STUDY_HUM, String(d["hum"].as<float>(), 2));
    }
  } else if (t == TOPIC_M5CAPSULE) {
    StaticJsonDocument<256> d;
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (!deserializeJson(d, js)) {
      if (d["client_count"].is<int>()) pushSensorValue(IDX_M5CAP_CLIENTS, String(d["client_count"].as<int>()));
    }
  }
}

/**
 * @brief タイトルや日時など、画面上部のヘッダーを描画します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */

void drawHeader() {
  const int W = M5.Display.width();
  canvas.drawFastHLine(0, L.HEADER_H - 1, W, COLOR_LINE);
  canvas.setTextColor(COLOR_FG, COLOR_BG);
  canvas.setFont(L.TITLE_FONT);
  canvas.setTextDatum(textdatum_t::top_center);
  canvas.drawString(String(DEVICE_TITLE), W / 2, 10);
  canvas.setFont(L.SMALL_FONT);
  canvas.setTextDatum(textdatum_t::top_left);
  String stat = String((WiFi.status() == WL_CONNECTED) ? "WiFi:OK" : "WiFi:NG");
  stat += "  ";
  stat += (mqtt.connected() ? "MQTT:OK" : "MQTT:NG");
  stat += "  ";
  stat += (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("-.-.-.-"));
  canvas.drawRightString(stat, W - RIGHT_PAD, 34);
  canvas.drawRightString(String("Season: ") + seasonLabelJP(g_currentSeason), W - RIGHT_PAD, 18);
}

/**
 * @brief 各行（ラベル・ゲージ・値）を1つ描画します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */

void drawRow(int sensorIdx, int y) {
  const int W = M5.Display.width();
  ColLayout C = ColHelper::compute();
  canvas.setFont(L.LABEL_FONT);
  canvas.setTextColor(COLOR_DIM, COLOR_BG);
  canvas.setTextDatum(textdatum_t::top_left);
  String labelTxt = ellipsizeToWidth(String(sensors[sensorIdx].label), C.labelW - 2);
  canvas.drawString(labelTxt, LEFT_PAD, y);
  const bool showBinary = isBinaryIndex(sensorIdx);
  const bool showNumericGauge = g_enable[sensorIdx];
  const bool showGauge = showBinary || showNumericGauge;
  canvas.setFont(L.VALUE_FONT);
  canvas.setTextColor(COLOR_FG, COLOR_BG);
  canvas.setTextDatum(textdatum_t::top_right);
  int valueLeftX = LEFT_PAD + C.labelW + C.gap;
  int valueAreaW = C.valueW + (showGauge ? 0 : (C.gap + C.gaugeW));
  int valueRightX = valueLeftX + valueAreaW;
  time_t nowt = time(nullptr), errShownTs = 0, infoTs = 0;
  bool isStale = sensorIsStale(sensorIdx, nowt, errShownTs, infoTs);
  String raw = sensors[sensorIdx].value.length() ? sensors[sensorIdx].value : "-";
  if (isStale) raw = "ERROR";
  String disp = formatValueForColumn2(raw, valueAreaW - 2);
  canvas.drawString(disp, valueRightX, y);
  /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
  if (showGauge) {
    int gx = LEFT_PAD + C.labelW + C.gap + C.valueW + C.gap;
    int gy = y + (L.LINE_H - L.GAUGE_H) / 2;
    int gw = C.gaugeW;
    if (showBinary) drawBinaryGauge(gx, gy, gw, L.GAUGE_H, binaryIsPositive(sensorIdx));
    else drawGaugeBar(gx, gy, gw, L.GAUGE_H, gaugePercent(sensorIdx, sensors[sensorIdx].value.toFloat()));
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (isStale) {
      canvas.setFont(L.SMALL_FONT);
      canvas.setTextColor(COLOR_DIM, COLOR_BG);
      canvas.setTextDatum(textdatum_t::top_left);
      String info = String("ERROR-15m: ") + formatDateTime(infoTs);
      while (info.length() && canvas.textWidth(info) > (gw - 4)) info.remove(info.length() - 1);
      canvas.drawString(info, gx + 2, gy + 1);
    }
  }
  (void)W;
}

/**
 * @brief 「Today at a glance」カードを条件付きで描画します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */

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
  canvas.drawString(String("THI: ") + (sensors[IDX_PICO_THI].value.length() ? sensors[IDX_PICO_THI].value : "-") + " (" + thiLabel(thi) + ")", x, y);
  y += 18;
  float co2_latest = sensors[IDX_PICO_CO2].value.toFloat();
  float co2_avg = co2Avg1h();
  String co2s = String("CO2: ") + (isnan(co2_latest) ? String("-") : String((int)co2_latest)) + " ppm";
  if (!isnan(co2_avg)) co2s += String("  /  1h avg ") + String((int)(co2_avg + 0.5f)) + " ppm";
  canvas.drawString(co2s, x, y);
  y += 18;
  dailyResetIfNeeded();
  String outs = "Outside: ";
  /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
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
  String sys = String("Wi-Fi ") + (WiFi.status() == WL_CONNECTED ? String(rssi) + " dBm" : String("NG")) + "   Heap " + String(freeHeap) + " kB";
  canvas.drawString(sys, cardMargin + cardW - 8, yStart + cardH - 6);
}

/**
 * @brief ヘルプや最終更新時刻など、画面下部のフッターを描画します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */

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

/**
 * @brief ダッシュボード全体を描画します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */

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
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
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
  /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
  if (showGlance && y + 4 + L.GLANCE_H <= bottom) { drawTodayCard(y + 4); }
  drawFooter();
  canvas.pushSprite(0, 0);
#if ENABLE_WATCHDOG && defined(ARDUINO_ARCH_ESP32)
  esp_task_wdt_reset();
#endif
  M5.Display.display();
  M5.Display.waitDisplay();
  canvas.deleteSprite();
}

/**
 * @brief タッチイベント（表示密度切替など）を処理します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */

void handleTouch() {
  if (!M5.Touch.isEnabled()) return;
  static uint32_t lastTap = 0;
  auto t = M5.Touch.getDetail();
  if (!t.isPressed()) return;
  if (millis() - lastTap < 250) return;
  lastTap = millis();
  if (t.y < L.HEADER_H) drawDashboard();
}

uint32_t lastRefresh = 0;

/**
 * @brief config.hの閾値/有効フラグを反映します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */

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

/**
 * @brief 季節ごとのゲージ範囲（任意）を上書き適用します。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */

void applySeasonalOverrides(Season s) {
#if ENABLE_SEASONAL_GAUGES
  {
    float mn = GAUGE_OUT_TEMP_MIN, mx = GAUGE_OUT_TEMP_MAX;
#if defined(GAUGE_OUT_TEMP_MIN_SPRING) && defined(GAUGE_OUT_TEMP_MAX_SPRING)
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (s == SEASON_SPRING) {
      mn = GAUGE_OUT_TEMP_MIN_SPRING;
      mx = GAUGE_OUT_TEMP_MAX_SPRING;
    }
#endif
#if defined(GAUGE_OUT_TEMP_MIN_SUMMER) && defined(GAUGE_OUT_TEMP_MAX_SUMMER)
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (s == SEASON_SUMMER) {
      mn = GAUGE_OUT_TEMP_MIN_SUMMER;
      mx = GAUGE_OUT_TEMP_MAX_SUMMER;
    }
#endif
#if defined(GAUGE_OUT_TEMP_MIN_AUTUMN) && defined(GAUGE_OUT_TEMP_MAX_AUTUMN)
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (s == SEASON_AUTUMN) {
      mn = GAUGE_OUT_TEMP_MIN_AUTUMN;
      mx = GAUGE_OUT_TEMP_MAX_AUTUMN;
    }
#endif
#if defined(GAUGE_OUT_TEMP_MIN_WINTER) && defined(GAUGE_OUT_TEMP_MAX_WINTER)
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (s == SEASON_WINTER) {
      mn = GAUGE_OUT_TEMP_MIN_WINTER;
      mx = GAUGE_OUT_TEMP_MAX_WINTER;
    }
#endif
    setGauge(IDX_OUT_TEMP, GAUGE_OUT_TEMP_ENABLE, mn, mx);
  }
  {
    float mn = GAUGE_PICO_TEMP_MIN, mx = GAUGE_PICO_TEMP_MAX;
#if defined(GAUGE_PICO_TEMP_MIN_SPRING) && defined(GAUGE_PICO_TEMP_MAX_SPRING)
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (s == SEASON_SPRING) {
      mn = GAUGE_PICO_TEMP_MIN_SPRING;
      mx = GAUGE_PICO_TEMP_MAX_SPRING;
    }
#endif
#if defined(GAUGE_PICO_TEMP_MIN_SUMMER) && defined(GAUGE_PICO_TEMP_MAX_SUMMER)
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (s == SEASON_SUMMER) {
      mn = GAUGE_PICO_TEMP_MIN_SUMMER;
      mx = GAUGE_PICO_TEMP_MAX_SUMMER;
    }
#endif
#if defined(GAUGE_PICO_TEMP_MIN_AUTUMN) && defined(GAUGE_PICO_TEMP_MAX_AUTUMN)
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (s == SEASON_AUTUMN) {
      mn = GAUGE_PICO_TEMP_MIN_AUTUMN;
      mx = GAUGE_PICO_TEMP_MAX_AUTUMN;
    }
#endif
#if defined(GAUGE_PICO_TEMP_MIN_WINTER) && defined(GAUGE_PICO_TEMP_MAX_WINTER)
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (s == SEASON_WINTER) {
      mn = GAUGE_PICO_TEMP_MIN_WINTER;
      mx = GAUGE_PICO_TEMP_MAX_WINTER;
    }
#endif
    setGauge(IDX_PICO_TEMP, GAUGE_PICO_TEMP_ENABLE, mn, mx);
  }
  {
    float mn = GAUGE_STUDY_TEMP_MIN, mx = GAUGE_STUDY_TEMP_MAX;
#if defined(GAUGE_STUDY_TEMP_MIN_SPRING) && defined(GAUGE_STUDY_TEMP_MAX_SPRING)
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (s == SEASON_SPRING) {
      mn = GAUGE_STUDY_TEMP_MIN_SPRING;
      mx = GAUGE_STUDY_TEMP_MAX_SPRING;
    }
#endif
#if defined(GAUGE_STUDY_TEMP_MIN_SUMMER) && defined(GAUGE_STUDY_TEMP_MAX_SUMMER)
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (s == SEASON_SUMMER) {
      mn = GAUGE_STUDY_TEMP_MIN_SUMMER;
      mx = GAUGE_STUDY_TEMP_MAX_SUMMER;
    }
#endif
#if defined(GAUGE_STUDY_TEMP_MIN_AUTUMN) && defined(GAUGE_STUDY_TEMP_MAX_AUTUMN)
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (s == SEASON_AUTUMN) {
      mn = GAUGE_STUDY_TEMP_MIN_AUTUMN;
      mx = GAUGE_STUDY_TEMP_MAX_AUTUMN;
    }
#endif
#if defined(GAUGE_STUDY_TEMP_MIN_WINTER) && defined(GAUGE_STUDY_TEMP_MAX_WINTER)
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (s == SEASON_WINTER) {
      mn = GAUGE_STUDY_TEMP_MIN_WINTER;
      mx = GAUGE_STUDY_TEMP_MAX_WINTER;
    }
#endif
    setGauge(IDX_STUDY_TEMP, GAUGE_STUDY_TEMP_ENABLE, mn, mx);
  }
  {
    float mn = GAUGE_PICO_THI_MIN, mx = GAUGE_PICO_THI_MAX;
#if defined(GAUGE_PICO_THI_MIN_SPRING) && defined(GAUGE_PICO_THI_MAX_SPRING)
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (s == SEASON_SPRING) {
      mn = GAUGE_PICO_THI_MIN_SPRING;
      mx = GAUGE_PICO_THI_MAX_SPRING;
    }
#endif
#if defined(GAUGE_PICO_THI_MIN_SUMMER) && defined(GAUGE_PICO_THI_MAX_SUMMER)
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (s == SEASON_SUMMER) {
      mn = GAUGE_PICO_THI_MIN_SUMMER;
      mx = GAUGE_PICO_THI_MAX_SUMMER;
    }
#endif
#if defined(GAUGE_PICO_THI_MIN_AUTUMN) && defined(GAUGE_PICO_THI_MAX_AUTUMN)
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (s == SEASON_AUTUMN) {
      mn = GAUGE_PICO_THI_MIN_AUTUMN;
      mx = GAUGE_PICO_THI_MAX_AUTUMN;
    }
#endif
#if defined(GAUGE_PICO_THI_MIN_WINTER) && defined(GAUGE_PICO_THI_MAX_WINTER)
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (s == SEASON_WINTER) {
      mn = GAUGE_PICO_THI_MIN_WINTER;
      mx = GAUGE_PICO_THI_MAX_WINTER;
    }
#endif
    setGauge(IDX_PICO_THI, GAUGE_PICO_THI_ENABLE, mn, mx);
  }
#endif
}

/**
 * @brief 一度だけ実行される初期化処理（Wi‑Fi/NTP/MQTT/描画準備など）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(0);
  setupWatchdog();
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
  initStaleArrays();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqttTune();
  ensureWiFi();
  initTimeIfEnabled();
  g_currentSeason = seasonFromLocalDate();
  applySeasonalOverrides(g_currentSeason);
  mqttReconnect();
  drawDashboard();
  lastRefresh = millis();
  lastHealth = millis();
}

/**
 * @brief メインループ（MQTT処理・描画更新・ヘルスチェック）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */

void loop() {
#if ENABLE_WATCHDOG && defined(ARDUINO_ARCH_ESP32)
  esp_task_wdt_reset();
#endif
  ensureWiFi();
  mqttReconnect();
  if (mqtt.connected()) mqtt.loop();
  handleTouch();
  /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
  if (millis() - lastRefresh > (REFRESH_SEC * 1000UL)) {
    drawDashboard();
    lastRefresh = millis();
  }
  /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
  if (millis() - lastHealth > HEALTH_CHECK_INTERVAL_MS) {
    lastHealth = millis();
    size_t freeKB = ESP.getFreeHeap() / 1024;
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (freeKB < LOW_HEAP_RESTART_KB) {
      delay(50);
      ESP.restart();
    }
    initTimeIfEnabled();
  }
  /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
  if (millis() - g_lastSeasonCheckMs > 3600000UL) {
    g_lastSeasonCheckMs = millis();
    Season s = seasonFromLocalDate();
    /**
 * @brief if の主な役割をまとめた関数です（詳細は本文参照）。
 * @note  初学者向けに：関数名と引数から役割を推測した説明です。実装に合わせて調整してください。
 */
    if (s != g_currentSeason) {
      g_currentSeason = s;
      applySeasonalOverrides(g_currentSeason);
      drawDashboard();
    }
  }
  delay(5);
}
