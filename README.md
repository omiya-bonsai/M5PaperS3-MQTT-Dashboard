# M5PaperS3 MQTT Dashboard (English README)

This project turns **M5Stack M5PaperS3 (ESP32‑S3 + e‑paper)** into a **single‑page, always‑on MQTT dashboard** for home/room environment data.  
It subscribes to multiple MQTT topics that deliver JSON payloads and **renders everything on one page**—no paging or scrolling.  
The UI and runtime are tuned for **low power** and **24/7 stability** on an e‑paper display.

> **Security tip:** Do **not** hard‑code secrets (Wi‑Fi/MQTT passwords) in a file that will be committed.  
> Consider splitting secrets to `config_local.h` and adding it to your `.gitignore`.

<img width="905" height="909" alt="rrr" src="https://github.com/user-attachments/assets/a12ffa93-5afc-4ea6-b41e-4391e965c5cb" />
---

## Highlights

- **One‑page layout** – All information fits on a single screen, no page navigation.
- **Auto density** – Picks *Normal / Compact / Ultra* based on display height to keep everything readable.
- **Gauges** – Numeric bars for temperature/humidity/CO₂, binary pattern bars for Rain/Cable.
- **Overflow handling** – If a value overflows the column, numeric‑like strings are rounded to **two decimals**; long text is ellipsized.
- **“Today at a glance”** – Drawn only if vertical space remains: THI, CO₂ (latest + 1‑hour average), outdoor Hi/Lo, max humidity, pressure.
- **24/7 hardening**
  - Wi‑Fi/MQTT auto‑reconnect with exponential backoff
  - Tuned keep‑alive / socket timeout / receive buffer
  - Optional ESP32 watchdog + periodic health checks
  - NTP time sync and periodic re‑sync
  - Low‑heap detection with safe restart
- **Interaction** – Tap the very **top of the screen** to refresh on demand.
- **Rendering** – Black on white, 1‑bit, optimized for e‑paper readability and power.

---

## Hardware & Software

- Device: **M5PaperS3**
- Network: Local MQTT broker (e.g., Mosquitto on a Raspberry Pi)
- Development environments (examples)
  - **Arduino IDE 2.x**
    - ESP32 boards by Espressif (make sure **PSRAM is enabled**)
    - Libraries: `M5Unified`, `PubSubClient`, `ArduinoJson`
  - **PlatformIO** (optional)
    - `platform = espressif32`
    - Select an **ESP32‑S3 board with PSRAM enabled**
    - Dependencies: `M5Unified`, `PubSubClient`, `ArduinoJson`

> The exact board name / PlatformIO `board` id may vary. Ensure you pick a config that matches **M5PaperS3 with PSRAM**.

---

## Project Layout (example)

```
/<your-project>/
├─ M5PaperS3_MQTT_Dashboard.ino   # main sketch
├─ config.h                        # settings (Wi‑Fi / MQTT / topics / UI / gauges)
└─ (optional) config_local.h       # secrets split-out (add to .gitignore)
```

---

## Setup (Arduino IDE)

1. Install ESP32 board definitions and make sure you can build for **M5PaperS3 / ESP32‑S3 with PSRAM enabled**.
2. Install libraries **M5Unified / PubSubClient / ArduinoJson**.
3. Edit `config.h`:
   - Wi‑Fi: `WIFI_SSID`, `WIFI_PASS`
   - MQTT: `MQTT_HOST`, `MQTT_PORT`, `MQTT_USER`, `MQTT_PASS`, `MQTT_CLIENT_ID`
   - Topics: `TOPIC_*` (see payload specs below)
   - UI: `DEVICE_TITLE`, `REFRESH_SEC` (periodic redraw seconds), etc.
   - NTP: `ENABLE_NTP`, `TZ_INFO`, `NTP_SERVER*`
   - 24/7: `ENABLE_WATCHDOG`, `WATCHDOG_TIMEOUT_SEC`, `LOW_HEAP_RESTART_KB`, etc.
   - Gauges: `GAUGE_*_ENABLE`, `*_MIN`, `*_MAX` (per sensor)
4. Connect M5PaperS3, select the serial port, and **upload**.
5. On first boot you should see **Booting... → Connecting Wi‑Fi**. Once connected, the dashboard will render.

---

## Screen Layout & Interaction

- **Header** – Title centered; right side shows `WiFi:OK/NG  MQTT:OK/NG  IP`.  
  Tap the **very top area** to force an immediate refresh.
- **Body** – Sections with headers and rows:
  - Example sections: `RAIN`, `PICO W`, `OUTSIDE`, `SYSTEM`, `Study`
  - Each row: **label (left) / value (right) / gauge (far right, optional)**
- **Today at a glance** – Appears only if vertical space is available.
- **Footer** – Two centered lines: a top **hint** (`TapTop:Refresh`) and the **last MQTT update** timestamp.

**Gauge behavior**
- Numeric bars: normalize **current value** in `[MIN..MAX]` to **0..100%**.
- Binary bars (Rain/Cable): patterned fill for **positive** state (≈70% fill).
- Value formatting: only when needed—round numeric‑like strings to **two decimals** or ellipsize long text.

---

## MQTT Topics & Payloads (examples)

> Topic names are **configurable** in `config.h`. Examples below match the default setup.

### 1) Rain sensor — `TOPIC_RAIN` (e.g., `home/weather/rain_sensor`)

```json
{
  "rain": true,           // raining? true/false
  "current": 123.4,       // ADC now
  "baseline": 117.8,      // ADC baseline
  "uptime": 12.5,         // hours
  "cable_ok": true        // cable OK
}
```

- Rows: `雨`, `雨 現在値(ADC)`, `雨 ベースライン`, `雨 稼働時間(h)`, `雨 ケーブル`
- Mapped to **ON/OFF** (rain) and **OK/NG** (cable).  
- Optional gauges: enable `GAUGE_RAIN_*`.

---

### 2) Pico (indoor) — `TOPIC_PICO` (e.g., `sensor_data`)

```json
{
  "temperature": 24.56,   // °C
  "humidity": 45.12,      // %
  "co2": 820,             // ppm
  "thi": 71.2             // Temperature-Humidity Index
}
```

- Rows: `Pico 温度(°C)`, `Pico 湿度(%)`, `リビング CO2(ppm)`, `Pico THI`
- CO₂ values are pushed into a fixed‑size ring buffer (up to 120 samples) to compute the **1‑hour average** in the card.
- THI labeled as **COOL / COMFY / WARM / HOT** using `THI_*_MAX` thresholds.

---

### 3) Outdoor sensor — `TOPIC_ENV4` (e.g., `env4`)

```json
{
  "temperature": 30.1,    // °C
  "humidity": 70.2,       // %
  "pressure": 1006.4      // hPa
}
```

- Rows: `外 温度(°C)`, `外 湿度(%)`, `外 気圧(hPa)`
- Updates **daily stats** (Hi/Lo temperature and max humidity) shown in the card.

---

### 4) Raspberry Pi CPU temperature — `TOPIC_RPI_TEMP` (e.g., `raspberry/temperature`)

- Accepts **free‑form text**. A numeric parser extracts a temperature value (examples):
  - `"54.1°C"`
  - `"temp=53.8C"`
  - `"CPU 55.2 C"`
- Row: `RPi5 CPU(°C)`

---

### 5) QZSS device temperature — `TOPIC_QZSS_TEMP` (e.g., `raspberry/qzss/temperature`)

```json
{ "temperature": 45.2 }
```

- Row: `QZSS CPU(°C)`

---

### 6) Study (M5StickC etc.) — `TOPIC_M5STICKC` (e.g., `m5stickc_co2/co2_data`)

```json
{
  "co2": 950,             // ppm
  "temp": 27.1,           // °C
  "hum": 45.8             // %
}
```

- Rows: `書斎 CO2(ppm)`, `書斎 温度(°C)`, `書斎 湿度(%)`

---

### 7) M5Capsule client count — `TOPIC_M5CAPSULE` (e.g., `m5capsule/clients`)

```json
{ "client_count": 3 }
```

- Row: `M5Capsule Clients` (the visibility can be toggled in the rows table)

---

## `config.h` – Key Settings

> The actual file contains many definitions; this is a map of the most important ones.

- **Network / MQTT**
  - `WIFI_SSID`, `WIFI_PASS`
  - `MQTT_HOST`, `MQTT_PORT`, `MQTT_USER`, `MQTT_PASS`, `MQTT_CLIENT_ID`
- **Topics**
  - `TOPIC_RAIN`, `TOPIC_PICO`, `TOPIC_ENV4`, `TOPIC_RPI_TEMP`, `TOPIC_QZSS_TEMP`, `TOPIC_M5STICKC`, `TOPIC_M5CAPSULE`
- **UI / Time / Refresh**
  - `DEVICE_TITLE` – header title
  - `REFRESH_SEC` – periodic redraw interval (seconds)
  - `ENABLE_NTP`, `TZ_INFO`, `NTP_SERVER1..3`
- **Gauge ranges (and enable flags)**
  - e.g., `GAUGE_PICO_TEMP_ENABLE`, `GAUGE_PICO_TEMP_MIN/MAX`, etc.
- **THI thresholds**
  - `THI_COOL_MAX`, `THI_COMFY_MAX`, `THI_WARM_MAX`
- **24/7 operation**
  - `ENABLE_WATCHDOG`, `WATCHDOG_TIMEOUT_SEC`
  - `LOW_HEAP_RESTART_KB` – restart when free heap drops under the threshold
  - `HEALTH_CHECK_INTERVAL_MS` – periodic health checks
  - `WIFI_MAX_BACKOFF_MS`, `MQTT_MAX_BACKOFF_MS` – reconnect backoff caps

---

## FAQ

**Q. The screen is blank or never updates.**  
A. Verify **PSRAM is enabled** in the board settings. The code waits for e‑paper refresh completion; power‑cycle or re‑flash if needed.

**Q. Values never appear.**  
A. Check that your broker address/port and **topic names** match `config.h`. Ensure your **JSON keys** match what the sketch expects.

**Q. The CO₂ 1‑hour average never shows.**  
A. You need **multiple CO₂ updates within the last hour**; the ring buffer aggregates only recent samples.

**Q. The timestamp shows `--`.**  
A. Enable NTP (`ENABLE_NTP = 1`) and ensure the device can reach your NTP servers (internet access).

**Q. The device reboots occasionally.**  
A. That’s a **self‑protective restart** (low heap / watchdog). Tune `LOW_HEAP_RESTART_KB` and `WATCHDOG_*` to your environment.

---

## Long‑Run Tips

- E‑paper is robust, but **periodic refresh** and a **sensible redraw cadence** help suppress ghosting.
- Use a **stable power source** and a decent USB cable.
- Consider **monitoring/auto‑restart** on the MQTT broker side (systemd, health checks).
- Use human‑readable **hostnames / client IDs** so you can identify devices on the network quickly.

---

## Contributing & License

- Issues and PRs are welcome. Please include reproduction steps, logs, and environment details when filing bugs.
- See the project **`LICENSE`** (e.g., MIT) for licensing details.

---

## Quick Reference – What to Change

- **Order / visibility** of rows & sections: edit the `rows[]` table.
- **Add a sensor**: add to the enum / `sensors[]` / `rows[]` / subscribe / message handler.
- **Gauge ON/OFF & ranges**: update `GAUGE_*` defines.
- **Title / refresh cadence / time sync**: `DEVICE_TITLE` / `REFRESH_SEC` / `ENABLE_NTP`.
- **Network / auth**: `WIFI_*` / `MQTT_*`.

---

## Screenshots (suggested)

- Header (centered title; connection status on the right)
- Body (labels left, values right, gauges when enabled)
- Today at a glance (when space allows)
- Footer (hint + last MQTT update)

> Add real screenshots from your device to the README if helpful.

---

## Disclaimer

This sketch is provided **as is**. In long‑term unattended deployments, behavior can be impacted by power quality, network reliability, and peripheral health. For critical environments, implement adequate monitoring and redundancy.

---

**Happy hacking & stay readable on e‑paper!**
