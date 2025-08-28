# M5PaperS3 MQTT ダッシュボード

**M5PaperS3** 用の電子ペーパーダッシュボードです。MQTT（Mosquitto）から受け取った各種センサー値を表示し、  
**ブローカー停止や Wi‑Fi 途切れに強い**堅牢な設計になっています。特に *「雨 稼働時間」* を誤ってリセットしないよう配慮しています。

> 🇬🇧 英語版は **[README.md](README.md)** をどうぞ。

<img width="905" height="909" alt="rrr" src="https://github.com/user-attachments/assets/d478cb74-6ca1-474d-a486-fe95d3db3f84" />

---

## 特徴

- **ブローカー障害に耐える**：Mosquitto の再起動やネットワーク断でも破綻しにくい。
- **「雨 稼働時間」の正しさ**：m5atomS3Lite + 自作レインセンサー（モバイルバッテリー駆動）の**デバイス稼働時間**を表示。  
  RAIN/DRY とは**独立**。MQTT 停止では**リセットされません**。実デバイスの**電源断→再給電**時のみリセットを採用。
- **見やすい UI**：ゲージ、無変動（STALE）検出、Today カード、Wi‑Fi/MQTT ステータス。
- **初心者向けコメント**：メインスケッチに日本語の詳細コメントを多数追加。
- **季節ごとのゲージ範囲（任意）**：マクロ定義で簡単に切替。

---

## ハード構成

- **表示**：M5PaperS3（ESP32‑S3、16MB Flash、PSRAM）
- **雨デバイス**：m5atomS3Lite + 自作レインセンサー（モバイルバッテリー給電）
- **その他の例**：Pico W（室内環境）、屋外環境、RPi/QZSS CPU 温度など

---

## リポジトリ構成

```
M5PaperS3-MQTT-Dashboard/
├─ M5PaperS3_MQTT_Dashboard.ino   # メインスケッチ（丁寧コメント付き）
├─ config.example.h               # 公開用テンプレ（→ 実機では config.h にリネーム）
└─ README.md / README-ja.md
```

> ⚠️ `config.h` は **コミット禁止**。`.gitignore` に入れてください。

---

## MQTT トピックとペイロード（例）

スケッチは以下を購読します（`config.h` で名称を変更可能）。

- `TOPIC_RAIN` → `home/weather/rain_sensor`  
  **JSON**:
  ```json
  { "rain": true, "current": 123.4, "baseline": 100.0, "uptime": 5.25, "cable_ok": true }
  ```
  - `uptime` は **雨デバイス（m5atomS3Lite + センサー）の稼働時間[h]**。  
    ダッシュボードはこの値を *雨 稼働時間* として扱い、MQTT 障害ではリセットしません。

- `TOPIC_PICO` → `sensor_data`（室内：温度/湿度/CO₂/THI）
- `TOPIC_ENV4` → `env4`（屋外：気温/湿度/気圧）
- `TOPIC_RPI_TEMP` → `raspberry/temperature`（文字列/数値）
- `TOPIC_QZSS_TEMP` → `raspberry/qzss/temperature`（JSON 温度）
- `TOPIC_M5STICKC` → `m5stickc_co2/co2_data`（CO₂ 等）
- `TOPIC_M5CAPSULE` → `m5capsule/clients`（クライアント数）

フィールドは存在チェックのうえ取り込みます。欠けていても動作します。

---

## ビルド環境

- **ボード（FQBN）**：`m5stack:esp32:m5stack_papers3`
- **コア**：`m5stack/esp32@3.2.2`
- **パーティション**：`app3M_fat9M_16MB`（M5PaperS3 既定）
- **PSRAM**：OPI、CPU 240 MHz（既定）

### 依存ライブラリ

| ライブラリ       | 動作確認版 |
| ---------------- | ---------- |
| M5Unified        | 0.2.7      |
| M5GFX            | 0.2.9      |
| WiFi (ESP32)     | 3.2.1      |
| PubSubClient     | 2.8        |
| ArduinoJson      | 7.4.2      |
| Preferences      | 3.2.1      |

> ライブラリマネージャから導入できます（WiFi/Preferences はボードパッケージに同梱）。

---

## 使い方（クイック）

1. `config.example.h` を **`config.h` にコピー**
2. 秘密情報・トピックを設定：
   ```cpp
   #define WIFI_SSID "YOUR_WIFI_SSID"
   #define WIFI_PASS "YOUR_WIFI_PASSWORD"
   #define MQTT_HOST "mqtt.example.local"
   #define MQTT_PORT 1883
   // ... topics ...
   ```
3. `M5PaperS3_MQTT_Dashboard.ino` を Arduino IDE で開く
4. **ボード**＝M5PaperS3、**ポート**を選びビルド
5. **書き込み**

### Arduino CLI（任意）

```bash
arduino-cli compile --fqbn m5stack:esp32:m5stack_papers3 .
arduino-cli upload  --fqbn m5stack:esp32:m5stack_papers3 -p <YOUR_PORT> .
```

---

## 設定のポイント

- `config.example.h` は **公開用**。実運用は `config.h` にリネームし、`.gitignore` へ。
- **無変動（STALE）検出**：`STALE_MINUTES` と `STALE_EXCLUDE_LIST` を調整。  
  稼働時間など、誤検出しやすい項目は除外推奨。
- **季節ゲージ（任意）**：`ENABLE_SEASONAL_GAUGES` を有効化し、季節別の最小/最大を定義。  
  `applySeasonalOverrides()` が自動で反映します。

---

## 「雨 稼働時間」の堅牢化

- Publisher（m5atomS3Lite）が `uptime`（h）を送信。
- ダッシュボード側は**前回の信頼値を保持**し、**逆行/欠損を弾く**ため：
  - Mosquitto 再起動や一時的な MQTT 断では**リセットされません**。
  - **実デバイスの再起動**（電源断→再給電）のみリセット採用。
- *DRY/RAIN* は別のブール値として表示。稼働時間とは**独立**です。

---

## トラブルシュート

### `error: 'thiLabel' was not declared in this scope`

`drawTodayCard()` より上に**前方宣言**を追加：

```cpp
const char* thiLabel(float thi);
```

または `THI_*` 定数の近くに **inline 定義**を配置：

```cpp
static inline const char* thiLabel(float thi) {
  if (isnan(thi)) return "--";
  if (thi < THI_COOL_MAX)  return "COOL";
  if (thi < THI_COMFY_MAX) return "COMFY";
  if (thi < THI_WARM_MAX)  return "WARM";
  return "HOT";
}
```

### ArduinoJson v7 の非推奨警告

`StaticJsonDocument` は非推奨ですが、そのまま動作します。  
将来的に `JsonDocument` へ移行すれば警告は消えます。

### 再定義警告（NUM_DIGITAL_PINS など）

ESP32 コアと M5PaperS3 バリアント由来の既知の警告で、無視して問題ありません。

---

## コントリビュート

Issue / PR 歓迎です。可能であれば日英併記にご協力ください。

---

## ライセンス

MIT
