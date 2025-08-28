// ============================================================================
// config.example.h — GitHub公開用テンプレート
// ----------------------------------------------------------------------------
// * このファイルをリポジトリに公開してください。
// * 実機では、このファイルを「config.h」にリネームし、あなたの環境値を記入して使います。
// * 秘密情報（SSID/パスワード等）が入った「config.h」は .gitignore で除外してください。
//
// 例：.gitignore に以下を追加
//   config.h
//
// まず編集する項目：
//   - WIFI_SSID / WIFI_PASS
//   - MQTT_HOST / MQTT_PORT / MQTT_USER / MQTT_PASS（必要なら）
//   - 各 TOPIC_*（ご自身のトピック命名に合わせる）
// ============================================================================
#pragma once

// ====== Network / MQTT ======
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"

#define MQTT_HOST "mqtt.example.local"
#define MQTT_PORT 1883
#define MQTT_USER ""  // 認証不要なら空文字
#define MQTT_PASS ""
#define MQTT_CLIENT_ID "M5PaperS3-Dashboard"  // 複数台あるならユニークに

// ====== Topics ======
// publisher 側（TOPIC_RAIN）の想定JSON：
//   { "rain": true, "current": 123.4, "baseline": 100.0, "uptime": 5.25, "cable_ok": true }
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
// 電子ペーパーのゴースト軽減用（必要に応じて使用）
#define GHOST_INTERVAL_SEC (1 * 3600)

// ====== Gauge ranges (1=enable / 0=disable) ======
// --- Rain ---
// 「雨 稼働時間(h)」は RAIN/DRY 無関係の“システム稼働時間”を表示します。
#define GAUGE_RAIN_CUR_ENABLE 0
#define GAUGE_RAIN_CUR_MIN 0
#define GAUGE_RAIN_CUR_MAX 4095

#define GAUGE_RAIN_BASE_ENABLE 0
#define GAUGE_RAIN_BASE_MIN 0
#define GAUGE_RAIN_BASE_MAX 4095

#define GAUGE_RAIN_UPTIME_ENABLE 1
#define GAUGE_RAIN_UPTIME_MIN 0
#define GAUGE_RAIN_UPTIME_MAX 48  // 例：48hスケール

// --- Pico W (室内) ---
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

// --- Outside (屋外) ---
#define GAUGE_OUT_TEMP_ENABLE 1
#define GAUGE_OUT_TEMP_MIN -10
#define GAUGE_OUT_TEMP_MAX 40

#define GAUGE_OUT_HUM_ENABLE 1
#define GAUGE_OUT_HUM_MIN 0
#define GAUGE_OUT_HUM_MAX 100

#define GAUGE_OUT_PRESS_ENABLE 1
#define GAUGE_OUT_PRESS_MIN 980
#define GAUGE_OUT_PRESS_MAX 1040

// --- System (CPU温度など) ---
#define GAUGE_RPI_TEMP_ENABLE 1
#define GAUGE_RPI_TEMP_MIN 30
#define GAUGE_RPI_TEMP_MAX 70

#define GAUGE_QZSS_TEMP_ENABLE 1
#define GAUGE_QZSS_TEMP_MIN 30
#define GAUGE_QZSS_TEMP_MAX 70

// --- Study room (書斎) ---
#define GAUGE_STUDY_CO2_ENABLE 1
#define GAUGE_STUDY_CO2_MIN 400
#define GAUGE_STUDY_CO2_MAX 1500

#define GAUGE_STUDY_TEMP_ENABLE 1
#define GAUGE_STUDY_TEMP_MIN 0
#define GAUGE_STUDY_TEMP_MAX 40

#define GAUGE_STUDY_HUM_ENABLE 1
#define GAUGE_STUDY_HUM_MIN 0
#define GAUGE_STUDY_HUM_MAX 100

// THI band（不快指数の目安。UI表示に使用）
#define THI_COOL_MAX 65
#define THI_COMFY_MAX 72
#define THI_WARM_MAX 78

// ====== STALE（無変動エラー）関連 ======
// 「雨 稼働時間」は DRY 中でも進み続けたり、停止中に更新が途切れたりします。
// 誤検出を避けるため、除外しておくのが安全です。
#define STALE_MINUTES 10
#define STALE_EXCLUDE_LIST \
  { IDX_RAIN_STATE, IDX_RAIN_BASE, IDX_RAIN_CUR, IDX_RAIN_CABLE, IDX_RAIN_UPTIME, IDX_STUDY_TEMP }

// ====== 季節ゲージ（任意） ======
// 有効化する場合は下記をアンコメントし、main 側 applySeasonalOverrides に合わせて設定。
// #define ENABLE_SEASONAL_GAUGES 1
// 例：
// #define GAUGE_OUT_TEMP_MIN_SPRING -5
// #define GAUGE_OUT_TEMP_MAX_SPRING 25
// #define GAUGE_OUT_TEMP_MIN_SUMMER 15
// #define GAUGE_OUT_TEMP_MAX_SUMMER 45
// #define GAUGE_OUT_TEMP_MIN_AUTUMN -5
// #define GAUGE_OUT_TEMP_MAX_AUTUMN 25
// #define GAUGE_OUT_TEMP_MIN_WINTER -10
// #define GAUGE_OUT_TEMP_MAX_WINTER 15
//
// 必要に応じて他のセンサーの季節分岐も定義できます。
// （main の applySeasonalOverrides() に対応ブロックを追加してください）
