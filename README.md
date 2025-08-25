# M5PaperS3 MQTT Dashboard (English README)

👉 [日本語版はこちら](./README-ja.md)

This sketch turns the **M5Stack M5PaperS3 (ESP32‑S3 + E‑paper)** into a **single‑screen, always‑on home sensor dashboard**.  
It subscribes to multiple MQTT topics carrying JSON payloads and renders them **without any paging**, tuned for **low power and 24/7 stability** on E‑paper.

<img width="905" height="909" alt="screenshot" src="https://github.com/user-attachments/assets/a12ffa93-5afc-4ea6-b41e-4391e965c5cb" />

---

## Features

- **One‑page layout**: no scrolling, no page switching.
- **Auto density**: chooses *Normal / Compact / Ultra* based on available vertical space.
- **Gauges**: intuitive bars for temperature / humidity / CO₂; patterned bars for binary states like rain/cable.
- **Overflow handling**: when a value doesn’t fit, numeric‑looking values are rounded to 2 decimals; long strings are ellipsized.
- **“Today at a glance” card**: when space permits, shows THI/CO₂/outdoor Hi/Lo, max humidity, pressure, etc.
- **24/7 hardening**:
  - Auto reconnect for Wi‑Fi/MQTT (exponential backoff)
  - Tuned keep‑alive, socket timeouts, receive buffers
  - Optional watchdog + periodic health checks
  - NTP time sync + periodic resync
  - Low‑heap detection with safe restart
- **Interaction**: tap the very top (header area) to **manually refresh**.
- **Rendering**: black on white (1‑bit) for crisp readability and low power.

---

## Hardware & Software

- Device: **M5PaperS3**
- Network: a home **MQTT broker** (e.g., Mosquitto on Raspberry Pi)
- Development (examples)
  - **Arduino IDE 2.x**
    - Install **ESP32** from Espressif via Boards Manager
    - **Enable PSRAM** (required)
    - Libraries: `M5Unified`, `PubSubClient`, `ArduinoJson`
  - **PlatformIO** (optional)
    - `platform = espressif32`
    - Select an **ESP32‑S3** board **with PSRAM**
    - Depends on: `M5Unified`, `PubSubClient`, `ArduinoJson`

> Board names and PlatformIO `board` IDs vary by environment. Make sure it’s M5PaperS3 (ESP32‑S3) **with PSRAM enabled**.

---

## Directory Layout (example)

```
/<your-project>/
├─ M5PaperS3_MQTT_Dashboard.ino   # Main sketch
├─ config.h                        # Settings (Wi‑Fi/MQTT/topics/UI/gauges/etc.)
└─ (optional) config_local.h       # For secrets if you prefer (add to .gitignore)
```

---

## Setup (Arduino IDE example)

1. Install the **ESP32** board package and ensure you can build for **M5PaperS3 / ESP32‑S3 with PSRAM**.
2. Install **M5Unified / PubSubClient / ArduinoJson** via the Library Manager.
3. Open `config.h` and set:
   - Wi‑Fi: `WIFI_SSID`, `WIFI_PASS`
   - MQTT: `MQTT_HOST`, `MQTT_PORT`, `MQTT_USER`, `MQTT_PASS`, `MQTT_CLIENT_ID`
   - Topics: `TOPIC_*` (match payload formats below)
   - UI: `DEVICE_TITLE`, `REFRESH_SEC` (periodic redraw interval in seconds), etc.
   - NTP: `ENABLE_NTP`, `TZ_INFO`, `NTP_SERVER*`
   - 24/7: `ENABLE_WATCHDOG`, `WATCHDOG_TIMEOUT_SEC`, `LOW_HEAP_RESTART_KB`, etc.
   - Gauges: `GAUGE_*_ENABLE`, `*_MIN`, `*_MAX` (per sensor)
4. Connect the M5PaperS3, choose the **serial port**, and **upload**.
5. On first boot you should see **Booting... → Connecting Wi‑Fi**. After connecting, the dashboard is drawn.

---

## UI & Interaction

- **Header**: title centered; right side shows `WiFi:OK/NG  MQTT:OK/NG  IP`.  
  Tap the **very top** to trigger an immediate refresh.
- **Body**: organized into sections (heading + rows).
  - Examples: `RAIN`, `PICO W`, `OUTSIDE`, `SYSTEM`, `Study`
  - Each row shows **label (left) / value (right) / optional gauge (far right)**.
- **Today at a glance**: appears only if there is spare vertical space.
- **Footer**: first line shows a hint `TapTop:Refresh`; second line shows the **last MQTT update time**.

**Gauge behavior**
- Numeric gauges: values are normalized from `MIN..MAX` to 0–100% and drawn as bars.
- Binary gauges (rain/cable): only the **positive** state gets a pattern fill (~70%) to stand out.
- Value formatting: only when a value would overflow; numerics round to **2 decimals**; long strings get `...`.

---

## MQTT Topics & Payloads (examples)

> **Topic names are configurable** in `config.h`. The following are defaults used by this sketch.

### 1) Rain sensor — `TOPIC_RAIN` (e.g., `home/weather/rain_sensor`)

```json
{
  "rain": true,
  "current": 123.4,
  "baseline": 117.8,
  "uptime": 12.5,
  "cable_ok": true
}
```

- Rendered as: `Rain / Rain Current(ADC) / Rain Baseline / Rain Uptime(h) / Rain Cable`
- `rain` → **ON/OFF**, `cable_ok` → **OK/NG**.
- Optional gauges: enable `GAUGE_RAIN_*` with `ENABLE=1`.

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

- Renders: `Pico Temp(°C) / Pico Humidity(%) / Living CO2(ppm) / Pico THI`
- CO₂ samples are stored in an internal ring buffer (max ~120). The **last 1‑hour average** is shown on the card.
- THI buckets into **COOL / COMFY / WARM / HOT** via thresholds (`THI_*_MAX`).

---

### 3) Outdoor sensor — `TOPIC_ENV4` (e.g., `env4`)

```json
{
  "temperature": 30.1,
  "humidity": 70.2,
  "pressure": 1006.4
}
```

- Renders: `Out Temp(°C) / Out Humidity(%) / Out Pressure(hPa)`
- Daily stats (today’s Hi/Lo and max humidity) are updated and shown on the card.

---

### 4) Raspberry Pi CPU temperature — `TOPIC_RPI_TEMP` (e.g., `raspberry/temperature`)

- Extracts the number from a **free‑form string**, e.g.:
  - `"54.1°C"`
  - `"temp=53.8C"`
  - `"CPU 55.2 C"`
- Renders: `RPi5 CPU(°C)`

---

### 5) QZSS device temperature — `TOPIC_QZSS_TEMP` (e.g., `raspberry/qzss/temperature`)

```json
{ "temperature": 45.2 }
```

- Renders: `QZSS CPU(°C)`

---

### 6) Study room (M5StickC, etc.) — `TOPIC_M5STICKC` (e.g., `m5stickc_co2/co2_data`)

```json
{
  "co2": 950,
  "temp": 27.1,
  "hum": 45.8
}
```

- Renders: `Study CO2(ppm) / Study Temp(°C) / Study Humidity(%)`

---

### 7) M5Capsule client count — `TOPIC_M5CAPSULE` (e.g., `m5capsule/clients`)

```json
{ "client_count": 3 }
```

- Renders: `M5Capsule Clients` (visibility can be toggled in the rows table).

---

## Key `config.h` Settings (guide)

- **Network / MQTT**
  - `WIFI_SSID`, `WIFI_PASS`
  - `MQTT_HOST`, `MQTT_PORT`, `MQTT_USER`, `MQTT_PASS`, `MQTT_CLIENT_ID`
- **Topics**
  - `TOPIC_RAIN`, `TOPIC_PICO`, `TOPIC_ENV4`, `TOPIC_RPI_TEMP`, `TOPIC_QZSS_TEMP`, `TOPIC_M5STICKC`, `TOPIC_M5CAPSULE`
- **UI / Time / Refresh**
  - `DEVICE_TITLE` (centered header title)
  - `REFRESH_SEC` (periodic redraw, seconds)
  - `ENABLE_NTP`, `TZ_INFO`, `NTP_SERVER1..3`
- **Gauge ranges (and enable flags)**
  - e.g., `GAUGE_PICO_TEMP_ENABLE`, `GAUGE_PICO_TEMP_MIN/MAX` per sensor
- **THI thresholds**
  - `THI_COOL_MAX`, `THI_COMFY_MAX`, `THI_WARM_MAX`
- **24/7 operation**
  - `ENABLE_WATCHDOG`, `WATCHDOG_TIMEOUT_SEC`
  - `LOW_HEAP_RESTART_KB` (restart when heap goes below threshold)
  - `HEALTH_CHECK_INTERVAL_MS`
  - `WIFI_MAX_BACKOFF_MS`, `MQTT_MAX_BACKOFF_MS`

---

## FAQ

**Q. The screen is blank / not updating.**  
A. Ensure **PSRAM is enabled** in your build settings. The sketch waits for `M5.Display.display()` to finish. Power‑cycle or re‑flash if needed.

**Q. Values never appear.**  
A. Verify the broker address/port and **topic names** match `config.h`. Ensure your **JSON keys** match what the sketch expects.

**Q. The CO₂ 1‑hour average never shows.**  
A. You need **multiple CO₂ updates within the last hour**; the ring buffer aggregates only recent samples.

**Q. The timestamp shows `--`.**  
A. Enable NTP (`ENABLE_NTP = 1`) and make sure the device can reach your NTP servers (internet access).

**Q. The device reboots occasionally.**  
A. That’s a **self‑protective restart** (low heap / watchdog). Tune `LOW_HEAP_RESTART_KB` and `WATCHDOG_*` for your environment.

---

## Long‑Run Tips

- E‑paper is robust, but **periodic refresh** and a **sensible redraw cadence** help suppress ghosting.
- Use a **stable power source** and a decent USB cable.
- Consider **monitoring/auto‑restart** on the MQTT broker side (systemd, health checks).
- Use human‑readable **hostnames / client IDs** so you can identify devices on the network quickly.

---

## Contributing & License

- Issues and PRs are welcome. Please include reproduction steps, logs, and environment details when filing bugs.
- See the project **`LICENSE`** for licensing details.

---

## Quick Reference – What to Change

- **Order / visibility** of rows & sections: edit the `rows[]` table.
- **Add a sensor**: add to the enum / `sensors[]` / `rows[]` / subscribe / message handler.
- **Gauge ON/OFF & ranges**: update `GAUGE_*` defines.
- **Title / refresh cadence / time sync**: `DEVICE_TITLE` / `REFRESH_SEC` / `ENABLE_NTP`.
- **Network / auth**: `WIFI_*` / `MQTT_*`.

---

## Disclaimer

This sketch is provided **as is**. In long‑term unattended deployments, behavior can be impacted by power quality, network reliability, and peripheral health. For critical environments, implement adequate monitoring and redundancy.

---

**Happy hacking & stay readable on E‑paper!**

