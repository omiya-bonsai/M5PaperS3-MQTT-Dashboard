// ============================================================================
// config.example.readable.h  —  学習用・公開用の設定テンプレート
// ----------------------------------------------------------------------------
// * これは PUBLIC に置く想定のテンプレートです。実運用では「config.h」にリネームして
//   自分の環境値（SSID/パスワード、MQTTブローカー等）に書き換えてください。
// * リポジトリ公開時はこのファイルのみコミットし、実ファイル（config.h）は .gitignore 推奨。
//
// ★最初に編集する項目（最低限）
//   - WIFI_SSID / WIFI_PASS
//   - MQTT_HOST / MQTT_PORT / MQTT_USER / MQTT_PASS（必要なら）
//   - 各 TOPIC_*（ご自身のトピック命名に合わせる）
//
// 読み方：各セクション先頭に「// ====== ... ======」の帯を付け、
//          重要ポイントには日本語で補足コメントを足しています。
// ============================================================================

#pragma once

// ====== Network / MQTT ======
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"

#define MQTT_HOST "mqtt.local"
#define MQTT_PORT 1883  // 通常は1883（認証なし/プレーン）
#define MQTT_USER ""    // 認証が不要なら空文字のまま  // 認証不要なら空文字
#define MQTT_PASS ""    // 認証が不要なら空文字のまま
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
#define GHOST_INTERVAL_SEC (1 * 3600)

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

// THI band (任意)
#define THI_COOL_MAX 65
#define THI_COMFY_MAX 72
#define THI_WARM_MAX 78

// #dt:2025-08-27 #tm:06:36
// センサー値が無変動の場合、ERRORを吐く
#define STALE_MINUTES 10  // 例：15分
// 1）バイナリ系など除外
// IDX_STUDY_TEMP は変動が少ないので、ERROR になりがち。
// そのため、監視の対象から外した。
// #dt:2025-08-27 #tm:16:11
#define STALE_EXCLUDE_LIST \
  { IDX_RAIN_STATE, IDX_RAIN_BASE, IDX_RAIN_CUR, IDX_RAIN_CABLE, IDX_STUDY_TEMP }

// ====== Seasonal Gauges (四季ごとゲージ範囲) ======
// #dt:2025-08-28 #tm:09:15
// #define ENABLE_SEASONAL_GAUGES 1

// 24節気ベースの始まり（固定日）
// #define SEASON_SPRING_START_MMDD 204   // 立春：2/4
// #define SEASON_SUMMER_START_MMDD 505   // 立夏：5/5
// #define SEASON_AUTUMN_START_MMDD 807   // 立秋：8/7
// #define SEASON_WINTER_START_MMDD 1107  // 立冬：11/7
// 体感的、経験的に、季節の開始日を設定
// #dt:2025-08-28 #tm:15:31
// 読み込めていないようなので、下記をコメントアウト
// そして、下記の月日情報をメインスケッチに明記する
// #dt:2025-08-28 #tm:15:37
// #define SEASON_SPRING_START_MMDD 304   // 立春：2/4
// #define SEASON_SUMMER_START_MMDD 505   // 立夏：5/5
// #define SEASON_AUTUMN_START_MMDD 1007   // 立秋：8/7
// #define SEASON_WINTER_START_MMDD 1217  // 立冬：11/7


// ---- Outside Temperature （屋外気温）----
#define GAUGE_OUT_TEMP_MIN_SPRING -5
#define GAUGE_OUT_TEMP_MAX_SPRING 25
#define GAUGE_OUT_TEMP_MIN_SUMMER 15
#define GAUGE_OUT_TEMP_MAX_SUMMER 45
#define GAUGE_OUT_TEMP_MIN_AUTUMN -5
#define GAUGE_OUT_TEMP_MAX_AUTUMN 25
#define GAUGE_OUT_TEMP_MIN_WINTER -10
#define GAUGE_OUT_TEMP_MAX_WINTER 15

// ---- Indoor (Pico) Temperature（屋内）----
#define GAUGE_PICO_TEMP_MIN_SPRING 10
#define GAUGE_PICO_TEMP_MAX_SPRING 28
#define GAUGE_PICO_TEMP_MIN_SUMMER 20
#define GAUGE_PICO_TEMP_MAX_SUMMER 40
#define GAUGE_PICO_TEMP_MIN_AUTUMN 10
#define GAUGE_PICO_TEMP_MAX_AUTUMN 28
#define GAUGE_PICO_TEMP_MIN_WINTER 5
#define GAUGE_PICO_TEMP_MAX_WINTER 26

// ---- Study Temperature（書斎）----
#define GAUGE_STUDY_TEMP_MIN_SPRING 10
#define GAUGE_STUDY_TEMP_MAX_SPRING 28
#define GAUGE_STUDY_TEMP_MIN_SUMMER 20
#define GAUGE_STUDY_TEMP_MAX_SUMMER 40
#define GAUGE_STUDY_TEMP_MIN_AUTUMN 10
#define GAUGE_STUDY_TEMP_MAX_AUTUMN 28
#define GAUGE_STUDY_TEMP_MIN_WINTER 5
#define GAUGE_STUDY_TEMP_MAX_WINTER 26

// ---- THI（快適域の目安：任意）----
#define GAUGE_PICO_THI_MIN_SPRING 55
#define GAUGE_PICO_THI_MAX_SPRING 75
#define GAUGE_PICO_THI_MIN_SUMMER 60
#define GAUGE_PICO_THI_MAX_SUMMER 85
#define GAUGE_PICO_THI_MIN_AUTUMN 55
#define GAUGE_PICO_THI_MAX_AUTUMN 75
#define GAUGE_PICO_THI_MIN_WINTER 45
#define GAUGE_PICO_THI_MAX_WINTER 65

/* 湿度や他のセンサーにも季節差を付けたい場合は同様に定義できます（必要に応じて）:
#define GAUGE_OUT_HUM_MIN_SPRING  20
#define GAUGE_OUT_HUM_MAX_SPRING  80
#define GAUGE_OUT_HUM_MIN_SUMMER  30
#define GAUGE_OUT_HUM_MAX_SUMMER  95
...（略）
※ main 側 applySeasonalOverrides() にブロックを追加してください。
*/
