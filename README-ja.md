# M5PaperS3 MQTT Dashboard（日本語版 README）

M5Stack 社の **M5PaperS3（ESP32-S3 + 電子ペーパー）** を、家庭内センサーの値を **一画面で見渡せる常時稼働ダッシュボード** として使うためのスケッチです。  
Wi‑Fi と MQTT を使って複数トピックの JSON を受信・可視化し、**ページ切り替えなし**で見やすく表示します。電子ペーパーの特性に合わせ、**低消費電力かつ 24/7 の安定運用**を重視しています。

> 初学者向けに、用語はできるだけ平易に説明しています。  
> GitHub への push 前に、`config.h` に **Wi‑Fi パスワード等の秘密情報を直書きしない**運用（例: `config_local.h` を `.gitignore` で除外）をご検討ください。

---

## 主な特徴

- **一枚レイアウト**：スクロールやページ送りなし。常に一画面で完結。
- **自動レイアウト密度**：画面高さに応じて *Normal / Compact / Ultra* を自動選択。
- **ゲージ表示**：温度・湿度・CO2 などはバーで直感表示。雨・ケーブルの二値はパターン表示。
- **あふれ対策**：値が狭い欄に収まらないときは、数値らしければ小数 2 桁に丸め、文字列は末尾を省略。
- **「今日のまとめ」カード**：空きスペースがあるときだけ、THI/CO2/外気の Hi/Lo/湿度最大/気圧 等を表示。
- **24/7 ハードニング**：
  - Wi‑Fi / MQTT の自動再接続（指数バックオフ）
  - キープアライブ・ソケットタイムアウト・受信バッファのチューニング
  - ウォッチドッグ（任意）と定期ヘルスチェック
  - NTP による時刻同期と定期再同期
  - 低ヒープ検知で安全に再起動
- **操作**：画面最上部（ヘッダ）をタップで手動リフレッシュ。
- **表示**：白地に黒（1bit）。電子ペーパーの読みやすさと省電力を両立。

---

## 対応ハード・ソフト

- デバイス：**M5PaperS3**
- ネットワーク：家庭内の MQTT ブローカー（例：Raspberry Pi 上の Mosquitto）
- 開発環境（例）
  - **Arduino IDE 2.x**
    - ボードマネージャで **ESP32** を導入（Espressif 公式）
    - **PSRAM 有効**（必須）
    - ライブラリ：`M5Unified`, `PubSubClient`, `ArduinoJson`
  - **PlatformIO**（任意）
    - `platform = espressif32`
    - ボードは **ESP32-S3 系**（PSRAM 有効）を選択
    - 依存：`M5Unified`, `PubSubClient`, `ArduinoJson`

> ボード名・PIO の `board` ID は環境によって異なります。M5PaperS3 かつ **PSRAM 有効**を満たす設定を選んでください。

---

## ディレクトリ構成（例）

```
/<your-project>/
├─ M5PaperS3_MQTT_Dashboard.ino   # 本スケッチ
├─ config.h                        # 設定（Wi‑Fi/MQTT/トピック/UI/ゲージ等）
└─ (任意) config_local.h           # 秘密情報を分離する場合（.gitignore 推奨）
```

---

## セットアップ手順（Arduino IDE 例）

1. **ESP32 ボード定義**を導入し、**M5PaperS3/ESP32‑S3 + PSRAM 有効**でビルドできる状態にします。
2. ライブラリマネージャで **M5Unified / PubSubClient / ArduinoJson** を導入します。
3. `config.h` を開き、以下を設定します。
   - Wi‑Fi：`WIFI_SSID`, `WIFI_PASS`
   - MQTT：`MQTT_HOST`, `MQTT_PORT`, `MQTT_USER`, `MQTT_PASS`, `MQTT_CLIENT_ID`
   - トピック：`TOPIC_*` 各種（後述のペイロード仕様と合わせてください）
   - UI：`DEVICE_TITLE`, `REFRESH_SEC`（定期再描画間隔 秒）など
   - NTP：`ENABLE_NTP`, `TZ_INFO`, `NTP_SERVER*`
   - 24/7：`ENABLE_WATCHDOG`, `WATCHDOG_TIMEOUT_SEC`, `LOW_HEAP_RESTART_KB` など
   - ゲージ：`GAUGE_*_ENABLE`, `*_MIN`, `*_MAX`（センサーごと）
4. M5PaperS3 を PC に接続し、**シリアルポート**を選択して**書き込み**ます。
5. 初回起動後、画面に **Booting... → Connecting Wi‑Fi** が表示されます。接続後、ダッシュボードが描画されます。

---

## 画面構成と操作

- **ヘッダ**：中央にタイトル、右側に `WiFi:OK/NG  MQTT:OK/NG  IP` を表示。  
  最上部タップで**即時リフレッシュ**。
- **本文**：セクション（見出し + 行）で構成。
  - 例：`RAIN`, `PICO W`, `OUTSIDE`, `SYSTEM`, `書斎`
  - 行は **ラベル（左）/ 値（右）/ 必要ならゲージ（最右）**。
- **Today at a glance**：空き高さがある場合のみ追加で表示。
- **フッタ**：上段にヒント `TapTop:Refresh`、下段に**最後の MQTT 更新時刻**。

**ゲージの仕様**
- 数値ゲージ：`MIN～MAX` を 0～100% に正規化してバー表示。
- 二値ゲージ（雨/ケーブル）：**陽性**のみパターン塗り（約 70% 塗り）で強調。
- 値の整形：文字幅が収まらない場合のみ丸め（数値は小数 2 桁）、文字列は `...` で省略。

---

## MQTT トピックとペイロード仕様（例）

> **トピック名は `config.h` で自由に変更可能**です。下記は本スケッチに合わせた既定名の例です。

### 1) 雨センサー — `TOPIC_RAIN`（例：`home/weather/rain_sensor`）

```json
{
  "rain": true,           // 降雨中 = true
  "current": 123.4,       // センサー現在値（ADC）
  "baseline": 117.8,      // センサー基準値
  "uptime": 12.5,         // 稼働時間（時間）
  "cable_ok": true        // ケーブル正常 = true
}
```

- 表示：`雨 / 雨 現在値(ADC) / 雨 ベースライン / 雨 稼働時間(h) / 雨 ケーブル`
- `rain` → **ON/OFF**、`cable_ok` → **OK/NG** として表示。  
- ゲージ（任意）：`GAUGE_RAIN_*` を `ENABLE=1` に。

---

### 2) Pico（屋内）— `TOPIC_PICO`（例：`sensor_data`）

```json
{
  "temperature": 24.56,   // °C
  "humidity": 45.12,      // %
  "co2": 820,             // ppm
  "thi": 71.2             // 暑さ指数 (Temperature-Humidity Index)
}
```

- 表示：`Pico 温度(°C) / Pico 湿度(%) / リビング CO2(ppm) / Pico THI`
- CO2 は内部リングバッファ（最大 120 件）に保存し、**直近 1 時間平均**を計算してカードに表示。
- THI はしきい値で **COOL / COMFY / WARM / HOT** に分類（`THI_*_MAX`）。

---

### 3) 屋外センサー — `TOPIC_ENV4`（例：`env4`）

```json
{
  "temperature": 30.1,    // °C
  "humidity": 70.2,       // %
  "pressure": 1006.4      // hPa
}
```

- 表示：`外 温度(°C) / 外 湿度(%) / 外 気圧(hPa)`
- 日次統計（当日 Hi/Lo と湿度最大）を更新し、カードに表示。

---

### 4) Raspberry Pi CPU 温度 — `TOPIC_RPI_TEMP`（例：`raspberry/temperature`）

- **自由形式の文字列**から温度数値を抽出します。例：
  - `"54.1°C"`
  - `"temp=53.8C"`
  - `"CPU 55.2 C"`
- 表示：`RPi5 CPU(°C)`

---

### 5) QZSS 機器温度 — `TOPIC_QZSS_TEMP`（例：`raspberry/qzss/temperature`）

```json
{ "temperature": 45.2 }
```

- 表示：`QZSS CPU(°C)`

---

### 6) 書斎（M5StickC 等）— `TOPIC_M5STICKC`（例：`m5stickc_co2/co2_data`）

```json
{
  "co2": 950,             // ppm
  "temp": 27.1,           // °C
  "hum": 45.8             // %
}
```

- 表示：`書斎 CO2(ppm) / 書斎 温度(°C) / 書斎 湿度(%)`

---

### 7) M5Capsule クライアント数 — `TOPIC_M5CAPSULE`（例：`m5capsule/clients`）

```json
{ "client_count": 3 }
```

- 表示：`M5Capsule Clients`（行テーブルで表示/非表示を切り替え可能）

---

## `config.h` の主な設定項目

> ※ 実ファイルでは多数の定義があります。以下は考え方のガイドです。

- **ネットワーク / MQTT**
  - `WIFI_SSID`, `WIFI_PASS`：Wi‑Fi 接続情報
  - `MQTT_HOST`, `MQTT_PORT`, `MQTT_USER`, `MQTT_PASS`, `MQTT_CLIENT_ID`
- **トピック**
  - `TOPIC_RAIN`, `TOPIC_PICO`, `TOPIC_ENV4`, `TOPIC_RPI_TEMP`, `TOPIC_QZSS_TEMP`, `TOPIC_M5STICKC`, `TOPIC_M5CAPSULE`
- **UI / 時刻 / 更新**
  - `DEVICE_TITLE`：ヘッダ中央のタイトル
  - `REFRESH_SEC`：定期再描画の間隔（秒）
  - `ENABLE_NTP`, `TZ_INFO`, `NTP_SERVER1..3`
- **ゲージ範囲（有効/無効も）**
  - 例：`GAUGE_PICO_TEMP_ENABLE`, `GAUGE_PICO_TEMP_MIN/MAX` など各センサーごと
- **THI（暑さ指数）しきい値**
  - `THI_COOL_MAX`, `THI_COMFY_MAX`, `THI_WARM_MAX`
- **24/7 運用**
  - `ENABLE_WATCHDOG`, `WATCHDOG_TIMEOUT_SEC`
  - `LOW_HEAP_RESTART_KB`：ヒープが閾値未満で再起動
  - `HEALTH_CHECK_INTERVAL_MS`：ヘルスチェック周期
  - `WIFI_MAX_BACKOFF_MS`, `MQTT_MAX_BACKOFF_MS`：再接続バックオフ上限

---

## よくある質問（FAQ）

**Q. 画面が真っ白/更新されない**  
A. ビルド設定で **PSRAM が有効**か確認してください。また、`M5.Display.display()` 後に更新完了待ちを行っています。電源の入れ直しや再書き込みで改善する場合があります。

**Q. MQTT の値が出ない**  
A. ブローカーのアドレスやポート、トピック名が `config.h` と一致しているか確認してください。JSON のキー名が異なると無視されます。

**Q. CO2 の 1 時間平均が表示されない**  
A. 直近 1 時間に **複数回の CO2 更新**が必要です（リングバッファで集計します）。

**Q. 時刻が `--` のまま**  
A. `ENABLE_NTP = 1` と NTP サーバの到達性を確認してください。インターネットに出られない場合は `--` になります。

**Q. たまに再起動する**  
A. 低ヒープ保護やウォッチドッグによる**自衛的な再起動**です。`LOW_HEAP_RESTART_KB` や `WATCHDOG_*` を調整してください。

---

## 長期運用のヒント

- 電子ペーパーは焼き付きを起こしにくいですが、**定期リフレッシュ**と**適度な更新間隔**でゴーストを抑制します。
- **電源は安定化**（ノイズ対策）し、USB ケーブルは品質の良いものを使用してください。
- MQTT ブローカー側にも **自動再起動/監視**（systemd, healthcheck 等）を用意すると堅牢性が上がります。
- 機器名（ホスト名 / クライアント ID）をわかりやすくし、ネットワーク上で識別しやすくすると運用が楽です。

---

## 貢献 / ライセンス

- バグ報告・改善提案は Issue で歓迎します。再現手順・ログ・環境情報を併記してください。
- ライセンスはプロジェクトの `LICENSE` を参照してください（例：MIT）。

---

## 変更しやすいポイントの早見表

- 表示順序やセクションの出し分け：**行テーブル**（`rows[]`）を編集
- 新センサー追加：**列挙体 / `sensors[]` / `rows[]` / 購読トピック / 受信処理**を一式追加
- ゲージの ON/OFF とレンジ：`GAUGE_*` 定義を編集
- タイトル/更新周期/時刻同期：`DEVICE_TITLE` / `REFRESH_SEC` / `ENABLE_NTP` など
- ネットワーク/認証：`WIFI_*` / `MQTT_*`

---

## スクリーンショット（参考）

- ヘッダ（タイトル中央 / 通信状態右上）
- 本文（ラベル左 / 値右 / 必要に応じてゲージ）
- Today at a glance（空きがある場合）
- フッタ（操作ヒント / 最終更新時刻）

> スクリーンショットは環境により異なります。必要に応じて README に追加してください。

---

## 免責

本スケッチは「現状のまま」提供されます。長期無人運用では、電源品質・ネットワーク品質・周辺機器の健全性など外部要因により挙動が変わる可能性があります。重要環境での利用時は十分な監視・冗長化をご検討ください。

---

**Happy hacking & stay readable on e‑paper!**
