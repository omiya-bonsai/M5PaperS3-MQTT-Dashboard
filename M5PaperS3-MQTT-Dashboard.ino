// ============================================================================
// M5PaperS3 MQTT Dashboard — 堅牢版（初心者向けに超ていねいコメント付き）
// ----------------------------------------------------------------------------
// このスケッチは、MQTTで配信される各種センサー値を M5PaperS3（電子ペーパー）に
// ダッシュボード表示するものです。特に、雨センサー系の「雨 稼働時間(h)」を
// “RAIN/DRYに関係なく”「デバイス（m5atomS3Lite+レインセンサー）のシステム稼働時間」
// として扱い、以下の障害に強くなるよう実装しています。
//
//   ✅ MQTTブローカ(Mosquitto)が落ちても、表示の稼働時間は減らない/リセットされない
//   ✅ Publisher（m5atomS3Lite）から届く uptime が一時的に小さく後退しても無視
//   ✅ 本当に電源断→再給電（実質的な再起動）したときだけリセット採用
//   ✅ 値はNVS(Preferences)に保存してM5再起動後も継続
//
// ── はじめに（超ざっくり手順）
//   1) 同梱の config.h を自分の環境に合わせて編集（Wi‑FiやMQTTの設定など）
//   2) Arduino IDE で以下のライブラリ/環境を準備
//       - M5Unified / M5GFX
//       - PubSubClient
//       - ArduinoJson (推奨 7.x)
//   3) ビルド＆書き込み。MQTTに繋がると自動で値が更新されます。
//
// ── 「雨 稼働時間(h)」について（重要）
//   - RAIN/DRY 無関係：あくまで「センサー側システムの通算稼働時間(時間)」です。
//   - Mosquitto停止中も、M5側がローカル時間を合成して伸ばし続けます。
//   - m5atomS3Lite の電源が落ちる（モバイルバッテリーを抜く等）と、再給電後の uptime が
//     ほぼ0から再開するはず → そのときだけ「真の再起動」と判断してリセットを受け入れます。
//
// 使い方メモ：
//   - 閾値は下の SystemUptimeKeeper 内（0.2h/15%など）で調整可能。
//   - STALE(無変動)監視から「雨 稼働時間」は除外済み（DRY中もERRORにならない）。
//
// ============================================================================

#include <M5Unified.h>
#include <M5GFX.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>  // ← NVSに稼働時間を保存/復元するために使用
#include <time.h>
#ifdef ARDUINO_ARCH_ESP32
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_task_wdt.h>
#include <esp_idf_version.h>
#endif
#include "config.h"  // ← あなたの環境値（SSID/MQTTなど）をここで設定

// ============================================================================
// 1) SystemUptimeKeeper：状態非依存の稼働時間合成エンジン
// ----------------------------------------------------------------------------
// 目的：
//  - MQTT停止やブローカ再起動で uptime が途切れても、M5側で“伸び続ける値”を合成する。
//  - publisherからの uptime が「ほんの少し小さい/揺れる」程度なら無視。
//  - 「明らかな再起動」だけを受け入れてリセットする（電源断→再給電時の挙動）。
//  - 値はNVSに保存して M5 再起動後も継続。
//
// 仕組み：
//  - 初回に uptime を受信するか、NVSに過去値があれば「seeded=true」になり、以降は
//    millis() 経過を時間(h)に換算して `displayUptimeH` を前進させます。
//  - onMqttUptime(uPubH)で新しい publisher 値が来たとき、
//    ・大きく後退 → “真の再起動”と判断できる条件のみ採用
//    ・十分先へ進んだ → そのまま更新（同期）
//    ・微小な差       → ノイズ扱い（M5の自走に任せる）
//  - 3分ごとにNVSへセーブ（フラッシュ寿命に配慮）。
// ============================================================================
class SystemUptimeKeeper {
public:
  void begin() {
    prefs.begin("sysup", false);  // 名前空間 "sysup" を開く（書込可）
    loadFromNVS();                // 過去値があれば復元
    lastTickMs = millis();
    if (!seeded && displayUptimeH > 0.0f) {
      // 過去値が0より大きければ、とりあえずそこから自走開始
      seeded = true;
    }
  }

  // publisher 側から uptime(h) を受け取ったときに呼ぶ
  void onMqttUptime(float uPubH) {
    // --- 閾値（必要に応じて調整OK） ---
    const float EPS_DEC = 0.05f;  // 減少を無視する閾値（0.05h ≒ 3分）
    const float EPS_INC = 0.20f;  // 前進を即採用する閾値（0.20h ≒ 12分）

    uint32_t nowMs = millis();
    if (!seeded) {
      // 初回：基準を確立（以後は自走）
      seeded = true;
      displayUptimeH = max(displayUptimeH, uPubH);
      basePubH = uPubH;
      baseMs = nowMs;
      lastTickMs = nowMs;
      saveToNVS();
      return;
    }

    // 参考：現在の自走推定値（使わなくても良いが、デバッグ時の目安）
    float estH = basePubH + (float)(nowMs - baseMs) / 3600000.0f;
    (void)estH;

    if (uPubH + EPS_DEC < displayUptimeH) {
      // 受信値が“明確に小さい” → ロールバックか再起動
      bool looksReboot =
        (uPubH < 0.2f) ||                                             // ほぼ0（電源入れ直し直後）
        (displayUptimeH > 1.0f && (uPubH / displayUptimeH) < 0.15f);  // 15%未満に急減
      if (looksReboot) {
        // 真の再起動 → 採用（0から積み直し）
        displayUptimeH = uPubH;
        basePubH = uPubH;
        baseMs = nowMs;
        lastTickMs = nowMs;
        saveToNVS();
      } else {
        // ノイズ → 無視（自走継続）
      }
    } else if (uPubH > displayUptimeH + EPS_INC) {
      // publisher が十分進んでいる → 同期して採用
      displayUptimeH = uPubH;
      basePubH = uPubH;
      baseMs = nowMs;
      lastTickMs = nowMs;
      saveToNVS();
    } else {
      // 微小差 → 自走に任せる。ドリフト抑制のため基準だけ同期
      basePubH = uPubH;
      baseMs = nowMs;
    }
  }

  // ループで定期的に呼ぶと、M5側で uptime が進み続ける（RAIN/DRY 無関係）
  void tick() {
    if (!seeded) return;  // まだ基準がないなら何もしない
    uint32_t nowMs = millis();
    uint32_t dms = nowMs - lastTickMs;
    if (dms >= 200) {                             // 細かすぎる書き換えを避けて200ms単位で進める
      displayUptimeH += (float)dms / 3600000.0f;  // ms → 時間(h)
      lastTickMs = nowMs;
      dirtySinceSaveMs += dms;
      if (dirtySinceSaveMs > 180000UL) {  // 3分ごとに保存
        saveToNVS();
        dirtySinceSaveMs = 0;
      }
    }
  }

  bool hasValue() const {
    return seeded;
  }
  float uptimeH() const {
    return displayUptimeH;
  }

private:
  Preferences prefs;
  bool seeded = false;            // 基準確立済みフラグ（NVS復元 or 初回受信でtrue）
  float displayUptimeH = 0.0f;    // 表示する稼働時間（M5側で前進）
  float basePubH = 0.0f;          // 最後に受け取ったpublisher側のuptime
  uint32_t baseMs = 0;            // その時のローカルms
  uint32_t lastTickMs = 0;        // 前回tick時刻
  uint32_t dirtySinceSaveMs = 0;  // 最後にセーブしてからの経過ms

  void loadFromNVS() {
    // 過去に保存されていればそれを復元
    seeded = prefs.getBool("seeded", false);
    displayUptimeH = prefs.getFloat("up_h", 0.0f);
  }
  void saveToNVS() {
    // 値の保存（フラッシュ寿命のため、tickでは間引き保存）
    prefs.putBool("seeded", seeded);
    prefs.putFloat("up_h", displayUptimeH);
  }
};

// ============================================================================
// 2) 季節判定（お好みで使用）
// ----------------------------------------------------------------------------
// 必要に応じて季節別にゲージ範囲を変える仕組み。ENABLE_SEASONAL_GAUGES 定義時のみ有効。
// ※ mmdd の境界は config.h でも上書き可能。
// ============================================================================
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
static inline const char* seasonLabelJP(Season s) {
  switch (s) {
    case SEASON_SPRING: return "Spring";
    case SEASON_SUMMER: return "Summer";
    case SEASON_AUTUMN: return "Autumn";
    default: return "Winter";
  }
}
static Season seasonFromLocalDate() {
  time_t now = time(nullptr);
  struct tm tmv;
  if (now <= 0 || !localtime_r(&now, &tmv)) return SEASON_SPRING;
  int mm = tmv.tm_mon + 1, dd = tmv.tm_mday, mmdd = mm * 100 + dd;
  if (mmdd >= SEASON_WINTER_START_MMDD || mmdd <= 303) return SEASON_WINTER;
  if (mmdd >= SEASON_SPRING_START_MMDD && mmdd <= 504) return SEASON_SPRING;
  if (mmdd >= SEASON_SUMMER_START_MMDD && mmdd <= 1006) return SEASON_SUMMER;
  return SEASON_AUTUMN;
}
static Season g_currentSeason = SEASON_SPRING;
static uint32_t g_lastSeasonCheckMs = 0;
void applySeasonalOverrides(Season s);

// ============================================================================
// 3) 各種タイミング/監視系の定数（必要に応じて調整）
// ============================================================================
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

// ============================================================================
// 4) 画面レイアウト系（大きく触らなくてOK）
// ============================================================================
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

// ============================================================================
// 5) ネットワーク/MQTT 接続状態の管理（指数バックオフ再接続など）
// ============================================================================
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
static uint32_t wifiLastTry = 0, wifiBackoff = 1000;
static uint32_t mqttLastTry = 0, mqttBackoff = 1000;
static uint32_t lastHealth = 0;   // ヘルスチェック用のタイムスタンプ
static uint32_t lastNtpSync = 0;  // NTP再同期のタイムスタンプ

// ============================================================================
// 6) センサー定義と表示行の構成
//    - label は左カラムに出るラベル文字
//    - value は右カラムの数値/テキスト
// ============================================================================
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

// STALE(無変動)監視から除外したいインデックスの一覧
#ifndef STALE_EXCLUDE_LIST
#define STALE_EXCLUDE_LIST \
  { IDX_RAIN_STATE, IDX_RAIN_BASE, IDX_RAIN_CUR, IDX_RAIN_CABLE, IDX_RAIN_UPTIME }
#endif
static const uint8_t kStaleExclude[] = STALE_EXCLUDE_LIST;
static inline bool isStaleExcluded(uint8_t idx) {
  for (size_t i = 0; i < sizeof(kStaleExclude) / sizeof(kStaleExclude[0]); ++i) {
    if (kStaleExclude[i] == idx) return true;
  }
  return false;
}

// 値の変化/最終更新時刻を追跡（STALE検出・情報表示に使用）
String g_lastValue[SENSOR_COUNT];
time_t g_lastChangeTs[SENSOR_COUNT];
time_t g_lastUpdateTs[SENSOR_COUNT];
time_t g_errorFirstShownTs[SENSOR_COUNT];

// 画面のセクション構成（ヘッダ行とデータ行の順番）
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

// ゲージの有効フラグと最小/最大
bool g_enable[SENSOR_COUNT];
float g_min[SENSOR_COUNT];
float g_max[SENSOR_COUNT];

// デフォルト（config.hで未定義ならここが効く）
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
#define GAUGE_RAIN_UPTIME_ENABLE 1
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

// THI（不快指数）帯
#ifndef THI_COOL_MAX
#define THI_COOL_MAX 65
#endif
#ifndef THI_COMFY_MAX
#define THI_COMFY_MAX 72
#endif
#ifndef THI_WARM_MAX
#define THI_WARM_MAX 78
#endif

// #dt:2025-08-29 #tm:04:47
// const char* thiLabel(float thi);

// CO2の簡易リングバッファ（1時間平均表示用）
time_t g_lastMqttUpdate = 0;
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

// 日次統計（屋外のHi/Loなど）
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

// 小物ユーティリティ
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
  while (*p && !((*p >= '0' && *p <= '9') || *p == '-')) ++p;
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
String ellipsizeToWidth(const String& s, int maxW) {
  if (canvas.textWidth(s) <= maxW) return s;
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
  if (idx == IDX_RAIN_STATE) return v == "RAIN";
  if (idx == IDX_RAIN_CABLE) return v == "NG";
  return false;
}

// STALE検出のための配列初期化
void initStaleArrays() {
  for (int i = 0; i < SENSOR_COUNT; ++i) {
    g_lastValue[i] = "";
    g_lastChangeTs[i] = 0;
    g_lastUpdateTs[i] = 0;
    g_errorFirstShownTs[i] = 0;
  }
}
// 新しい値を反映（最終更新や変化時刻も更新）
void pushSensorValue(uint8_t idx, const String& v) {
  if (idx >= SENSOR_COUNT) return;
  time_t now = time(nullptr);
  if (g_lastValue[idx].length() == 0) g_lastChangeTs[idx] = now;
  else if (v != g_lastValue[idx]) {
    g_lastChangeTs[idx] = now;
    g_errorFirstShownTs[idx] = 0;
  }
  g_lastValue[idx] = v;
  g_lastUpdateTs[idx] = now;
  sensors[idx].value = v;
}
// 一定時間変化がないか（STALE）を判定（除外リスト対象はスキップ）
bool sensorIsStale(uint8_t idx, time_t now, time_t& outErrorShown, time_t& outInfoTime) {
  if (idx >= SENSOR_COUNT) return false;
  if (isStaleExcluded(idx)) return false;
  if (g_lastChangeTs[idx] == 0) return false;
  long sinceChange = (long)(now - g_lastChangeTs[idx]);
  if (sinceChange >= STALE_SEC) {
    if (g_errorFirstShownTs[idx] == 0) { g_errorFirstShownTs[idx] = now; }
    outErrorShown = g_errorFirstShownTs[idx];
    outInfoTime = g_lastChangeTs[idx];
    return true;
  } else {
    g_errorFirstShownTs[idx] = 0;
    return false;
  }
}

// ── 画面描画（ゲージ/ヘッダ/フッタ 等）
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
  if (showGauge) {
    int gx = LEFT_PAD + C.labelW + C.gap + C.valueW + C.gap;
    int gy = y + (L.LINE_H - L.GAUGE_H) / 2;
    int gw = C.gaugeW;
    if (showBinary) drawBinaryGauge(gx, gy, gw, L.GAUGE_H, binaryIsPositive(sensorIdx));
    else drawGaugeBar(gx, gy, gw, L.GAUGE_H, gaugePercent(sensorIdx, sensors[sensorIdx].value.toFloat()));
    if (isStale) {
      canvas.setFont(L.SMALL_FONT);
      canvas.setTextColor(COLOR_DIM, COLOR_BG);
      canvas.setTextDatum(textdatum_t::top_left);
      String info = String("ERROR-") + String(STALE_MINUTES) + String("m: ") + formatDateTime(infoTs);
      while (info.length() && canvas.textWidth(info) > (gw - 4)) info.remove(info.length() - 1);
      canvas.drawString(info, gx + 2, gy + 1);
    }
  }
  (void)W;
}


// #dt:2025-08-29 #tm:04:49
static inline const char* thiLabel(float thi) {
  if (isnan(thi)) return "--";
  if (thi < THI_COOL_MAX) return "COOL";
  if (thi < THI_COMFY_MAX) return "COMFY";
  if (thi < THI_WARM_MAX) return "WARM";
  return "HOT";
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

// タッチ操作（上部タップで画面再描画）
void handleTouch() {
  if (!M5.Touch.isEnabled()) return;
  static uint32_t lastTap = 0;
  auto t = M5.Touch.getDetail();
  if (!t.isPressed()) return;
  if (millis() - lastTap < 250) return;
  lastTap = millis();
  if (t.y < L.HEADER_H) drawDashboard();
}

// ============================================================================
// 7) 設定適用や季節ゲージの上書き、起動/ループ
// ============================================================================
uint32_t lastRefresh = 0;

// config.h のゲージ設定を反映
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

// 季節ごとのゲージ範囲（有効化しているときのみ上書き）
void applySeasonalOverrides(Season s) {
#if ENABLE_SEASONAL_GAUGES
  {
    float mn = GAUGE_OUT_TEMP_MIN, mx = GAUGE_OUT_TEMP_MAX;
#if defined(GAUGE_OUT_TEMP_MIN_SPRING) && defined(GAUGE_OUT_TEMP_MAX_SPRING)
    if (s == SEASON_SPRING) {
      mn = GAUGE_OUT_TEMP_MIN_SPRING;
      mx = GAUGE_OUT_TEMP_MAX_SPRING;
    }
#endif
#if defined(GAUGE_OUT_TEMP_MIN_SUMMER) && defined(GAUGE_OUT_TEMP_MAX_SUMMER)
    if (s == SEASON_SUMMER) {
      mn = GAUGE_OUT_TEMP_MIN_SUMMER;
      mx = GAUGE_OUT_TEMP_MAX_SUMMER;
    }
#endif
#if defined(GAUGE_OUT_TEMP_MIN_AUTUMN) && defined(GAUGE_OUT_TEMP_MAX_AUTUMN)
    if (s == SEASON_AUTUMN) {
      mn = GAUGE_OUT_TEMP_MIN_AUTUMN;
      mx = GAUGE_OUT_TEMP_MAX_AUTUMN;
    }
#endif
#if defined(GAUGE_OUT_TEMP_MIN_WINTER) && defined(GAUGE_OUT_TEMP_MAX_WINTER)
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
    if (s == SEASON_SPRING) {
      mn = GAUGE_PICO_TEMP_MIN_SPRING;
      mx = GAUGE_PICO_TEMP_MAX_SPRING;
    }
#endif
#if defined(GAUGE_PICO_TEMP_MIN_SUMMER) && defined(GAUGE_PICO_TEMP_MAX_SUMMER)
    if (s == SEASON_SUMMER) {
      mn = GAUGE_PICO_TEMP_MIN_SUMMER;
      mx = GAUGE_PICO_TEMP_MAX_SUMMER;
    }
#endif
#if defined(GAUGE_PICO_TEMP_MIN_AUTUMN) && defined(GAUGE_PICO_TEMP_MAX_AUTUMN)
    if (s == SEASON_AUTUMN) {
      mn = GAUGE_PICO_TEMP_MIN_AUTUMN;
      mx = GAUGE_PICO_TEMP_MAX_AUTUMN;
    }
#endif
#if defined(GAUGE_PICO_TEMP_MIN_WINTER) && defined(GAUGE_PICO_TEMP_MAX_WINTER)
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
    if (s == SEASON_SPRING) {
      mn = GAUGE_STUDY_TEMP_MIN_SPRING;
      mx = GAUGE_STUDY_TEMP_MAX_SPRING;
    }
#endif
#if defined(GAUGE_STUDY_TEMP_MIN_SUMMER) && defined(GAUGE_STUDY_TEMP_MAX_SUMMER)
    if (s == SEASON_SUMMER) {
      mn = GAUGE_STUDY_TEMP_MIN_SUMMER;
      mx = GAUGE_STUDY_TEMP_MAX_SUMMER;
    }
#endif
#if defined(GAUGE_STUDY_TEMP_MIN_AUTUMN) && defined(GAUGE_STUDY_TEMP_MAX_AUTUMN)
    if (s == SEASON_AUTUMN) {
      mn = GAUGE_STUDY_TEMP_MIN_AUTUMN;
      mx = GAUGE_STUDY_TEMP_MAX_AUTUMN;
    }
#endif
#if defined(GAUGE_STUDY_TEMP_MIN_WINTER) && defined(GAUGE_STUDY_TEMP_MAX_WINTER)
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
    if (s == SEASON_SPRING) {
      mn = GAUGE_PICO_THI_MIN_SPRING;
      mx = GAUGE_PICO_THI_MAX_SPRING;
    }
#endif
#if defined(GAUGE_PICO_THI_MIN_SUMMER) && defined(GAUGE_PICO_THI_MAX_SUMMER)
    if (s == SEASON_SUMMER) {
      mn = GAUGE_PICO_THI_MIN_SUMMER;
      mx = GAUGE_PICO_THI_MAX_SUMMER;
    }
#endif
#if defined(GAUGE_PICO_THI_MIN_AUTUMN) && defined(GAUGE_PICO_THI_MAX_AUTUMN)
    if (s == SEASON_AUTUMN) {
      mn = GAUGE_PICO_THI_MIN_AUTUMN;
      mx = GAUGE_PICO_THI_MAX_AUTUMN;
    }
#endif
#if defined(GAUGE_PICO_THI_MIN_WINTER) && defined(GAUGE_PICO_THI_MAX_WINTER)
    if (s == SEASON_WINTER) {
      mn = GAUGE_PICO_THI_MIN_WINTER;
      mx = GAUGE_PICO_THI_MAX_WINTER;
    }
#endif
    setGauge(IDX_PICO_THI, GAUGE_PICO_THI_ENABLE, mn, mx);
  }
#endif
}

// -------------------- ネットワーク/NTP/Watchdog 補助 --------------------
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
#if ENABLE_WATCHDOG && defined(ARDUINO_ARCH_ESP32)
    esp_task_wdt_reset();
#endif
  }
  if (WiFi.status() == WL_CONNECTED) wifiBackoff = 1000;
  else wifiBackoff = cap_u32(wifiBackoff * 2, WIFI_MAX_BACKOFF_MS);
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
  bool ok = strlen(MQTT_USER) ? mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS) : mqtt.connect(MQTT_CLIENT_ID);
  if (ok) {
    mqttBackoff = 1000;
    mqttSubscribeAll();
  } else {
    mqttBackoff = cap_u32(mqttBackoff * 2, MQTT_MAX_BACKOFF_MS);
  }
}

// ============================================================================
// 8) MQTTメッセージ受信：各トピックのJSONを解析して pushSensorValue() に反映
//    - ※「雨 稼働時間」は onMqttUptime() でロバストに扱います。
// ============================================================================
static SystemUptimeKeeper g_uptime;  // 稼働時間エンジン
static uint32_t g_uptimePushMs = 0;  // UI更新の間引き用
#ifndef UPTIME_PUSH_INTERVAL_MS
#define UPTIME_PUSH_INTERVAL_MS 15000UL  // 稼働時間表示の更新間隔（ms）
#endif

void onMqttMessage(char* topic, byte* payload, uint16_t len) {
  String t(topic), js;
  js.reserve(len + 1);
  for (uint16_t i = 0; i < len; ++i) js += (char)payload[i];
  g_lastMqttUpdate = time(nullptr);

  if (t == TOPIC_RAIN) {
    StaticJsonDocument<512> d;
    if (!deserializeJson(d, js)) {
      if (d["rain"].is<bool>()) pushSensorValue(IDX_RAIN_STATE, (d["rain"].as<bool>() ? "RAIN" : "DRY"));
      if (d["current"].is<float>()) pushSensorValue(IDX_RAIN_CUR, String(d["current"].as<float>(), 1));
      if (d["baseline"].is<float>()) pushSensorValue(IDX_RAIN_BASE, String(d["baseline"].as<float>(), 1));
      if (d["cable_ok"].is<bool>()) pushSensorValue(IDX_RAIN_CABLE, d["cable_ok"].as<bool>() ? "OK" : "NG");
      if (d["uptime"].is<float>()) {
        // ★ここがキモ：publisher値をヒューリスティックで採用/破棄しながら、M5側で伸ばす
        g_uptime.onMqttUptime(d["uptime"].as<float>());
        pushSensorValue(IDX_RAIN_UPTIME, String(g_uptime.uptimeH(), 2));
        g_uptimePushMs = millis();
      }
    }
  } else if (t == TOPIC_PICO) {
    StaticJsonDocument<512> d;
    if (!deserializeJson(d, js)) {
      if (d["temperature"].is<float>()) pushSensorValue(IDX_PICO_TEMP, String(d["temperature"].as<float>(), 2));
      if (d["humidity"].is<float>()) pushSensorValue(IDX_PICO_HUM, String(d["humidity"].as<float>(), 2));
      if (d["co2"].is<int>()) {
        int v = d["co2"].as<int>();
        pushSensorValue(IDX_PICO_CO2, String(v));
        co2Push(time(nullptr), (float)v);
      }
      if (d["thi"].is<float>()) pushSensorValue(IDX_PICO_THI, String(d["thi"].as<float>(), 1));
    }
  } else if (t == TOPIC_ENV4) {
    StaticJsonDocument<512> d;
    if (!deserializeJson(d, js)) {
      bool ht = false, hh = false;
      float tv = 0, hv = 0;
      if (d["temperature"].is<float>()) {
        tv = d["temperature"].as<float>();
        pushSensorValue(IDX_OUT_TEMP, String(tv, 1));
        ht = true;
      }
      if (d["humidity"].is<float>()) {
        hv = d["humidity"].as<float>();
        pushSensorValue(IDX_OUT_HUM, String(hv, 2));
        hh = true;
      }
      if (d["pressure"].is<float>()) { pushSensorValue(IDX_OUT_PRESS, String(d["pressure"].as<float>(), 2)); }
      if (ht || hh) dailyUpdate(tv, ht, hv, hh);
    }
  } else if (t == TOPIC_RPI_TEMP) {
    float v = parseTempFromText(js.c_str());
    if (!isnan(v)) pushSensorValue(IDX_RPI_TEMP, String(v, 1));
  } else if (t == TOPIC_QZSS_TEMP) {
    StaticJsonDocument<256> d;
    if (!deserializeJson(d, js)) {
      if (d["temperature"].is<float>()) pushSensorValue(IDX_QZSS_TEMP, String(d["temperature"].as<float>(), 1));
    }
  } else if (t == TOPIC_M5STICKC) {
    StaticJsonDocument<512> d;
    if (!deserializeJson(d, js)) {
      if (d["co2"].is<int>()) pushSensorValue(IDX_STUDY_CO2, String(d["co2"].as<int>()));
      if (d["temp"].is<float>()) pushSensorValue(IDX_STUDY_TEMP, String(d["temp"].as<float>(), 1));
      if (d["hum"].is<float>()) pushSensorValue(IDX_STUDY_HUM, String(d["hum"].as<float>(), 2));
    }
  } else if (t == TOPIC_M5CAPSULE) {
    StaticJsonDocument<256> d;
    if (!deserializeJson(d, js)) {
      if (d["client_count"].is<int>()) pushSensorValue(IDX_M5CAP_CLIENTS, String(d["client_count"].as<int>()));
    }
  }
}

// ============================================================================
// 9) setup()/loop()：起動時の初期化とメインループ
// ============================================================================
void setup() {
  // --- 画面に「Booting...」などを出しつつ、最低限の初期化 ---
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

  // ゲージとエラー検出の準備
  for (int i = 0; i < SENSOR_COUNT; ++i) {
    g_enable[i] = false;
    g_min[i] = 0;
    g_max[i] = 1;
  }
  applyGaugeFromConfig();
  initStaleArrays();

  // MQTT/NTP/季節設定など
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqttTune();
  ensureWiFi();
  initTimeIfEnabled();
  g_currentSeason = seasonFromLocalDate();
  applySeasonalOverrides(g_currentSeason);

  // 稼働時間エンジンの起動（NVS復元→即UI反映）
  g_uptime.begin();
  if (g_uptime.hasValue()) {
    pushSensorValue(IDX_RAIN_UPTIME, String(g_uptime.uptimeH(), 2));
  }

  mqttReconnect();
  drawDashboard();
  lastRefresh = millis();
  lastHealth = millis();
  g_uptimePushMs = millis();
}

void loop() {
#if ENABLE_WATCHDOG && defined(ARDUINO_ARCH_ESP32)
  esp_task_wdt_reset();
#endif
  ensureWiFi();
  mqttReconnect();
  if (mqtt.connected()) mqtt.loop();
  handleTouch();

  // ここが重要：MQTTがなくても uptime は前進し続ける（RUN/DRY 無関係）
  g_uptime.tick();
  if (g_uptime.hasValue()) {
    uint32_t nowMs = millis();
    if (nowMs - g_uptimePushMs >= UPTIME_PUSH_INTERVAL_MS) {
      pushSensorValue(IDX_RAIN_UPTIME, String(g_uptime.uptimeH(), 2));
      g_uptimePushMs = nowMs;
    }
  }

  // 画面の定期再描画/ヘルスチェック/NTP再同期/季節更新など
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
  if (millis() - g_lastSeasonCheckMs > 3600000UL) {
    g_lastSeasonCheckMs = millis();
    Season s = seasonFromLocalDate();
    if (s != g_currentSeason) {
      g_currentSeason = s;
      applySeasonalOverrides(g_currentSeason);
      drawDashboard();
    }
  }
  delay(5);
}
