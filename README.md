# M5PaperS3 MQTT Dashboard

A highly robust e‑paper dashboard for **M5PaperS3** that renders live sensor data from an MQTT broker (Mosquitto).  
It is designed to **survive broker outages and Wi‑Fi hiccups** without corrupting metrics such as *Rain Uptime*.

> 🇯🇵 Looking for Japanese docs? See **[README-ja.md](README-ja.md)**.

<img width="905" height="909" alt="rrr" src="https://github.com/user-attachments/assets/d0ebcfca-0158-4ca4-8d13-bccb8f6b9326" />

---

## Highlights

- **Broker‑outage proof**: The dashboard tolerates Mosquitto restarts and network drops. Metrics are debounced and cached.
- **Correct “Rain Uptime”**: Uptime represents the *rain device’s runtime* (m5atomS3Lite + custom rain sensor), **independent of RAIN/DRY state**.  
  Resets **only** when a true power cycle of the *publisher device* is detected (not when MQTT stops).
- **Readable UI**: Clear gauges, stale detection, “Today at a glance” card, and a header with live connectivity status.
- **Beginner‑friendly code**: Extensive Japanese comments in the sketch to help learning and maintenance.
- **Optional seasonal gauge ranges**: Simple macros to vary gauge ranges by seasons.

---

## Hardware

- **Display**: M5PaperS3 (ESP32‑S3, 16MB Flash, PSRAM)
- **Rain device**: m5atomS3Lite + custom rain sensor (powered by a mobile battery)
- **Other sensors (examples)**: Pico W indoor env, outdoor env, RPi/QZSS CPU temps, etc.

---

## Repository Layout

```
M5PaperS3-MQTT-Dashboard/
├─ M5PaperS3_MQTT_Dashboard.ino   # Main sketch (with heavy comments)
├─ config.example.h               # Public template (copy to config.h)
└─ README.md / README-ja.md
```

> ⚠️ **Never commit `config.h`**. Keep it in `.gitignore`.

---

## MQTT Topics & Payloads (examples)

The sketch subscribes to these topics (rename as needed in `config.h`):

- `TOPIC_RAIN` → `home/weather/rain_sensor`  
  **JSON**:
  ```json
  { "rain": true, "current": 123.4, "baseline": 100.0, "uptime": 5.25, "cable_ok": true }
  ```
  - `uptime` is the publisher’s runtime **in hours** (m5atomS3Lite + rain sensor).  
    The dashboard treats this as *Rain Uptime* and protects it from MQTT outages.

- `TOPIC_PICO` → `sensor_data` (temperature/humidity/CO₂/THI)
- `TOPIC_ENV4` → `env4` (outdoor temp/humidity/pressure)
- `TOPIC_RPI_TEMP` → `raspberry/temperature` (string/number)
- `TOPIC_QZSS_TEMP` → `raspberry/qzss/temperature` (JSON temperature)
- `TOPIC_M5STICKC` → `m5stickc_co2/co2_data` (CO₂/etc.)
- `TOPIC_M5CAPSULE` → `m5capsule/clients` (client count)

You can freely adapt field names; the sketch checks and ignores missing ones.

---

## Build Environment

- **Board (FQBN)**: `m5stack:esp32:m5stack_papers3`
- **Core**: `m5stack/esp32@3.2.2`  
- **Partition**: `app3M_fat9M_16MB` (default for M5PaperS3 variant)
- **PSRAM**: OPI, CPU 240 MHz (default board options)

### Required Libraries

| Library        | Version (tested) |
| -------------- | ---------------- |
| M5Unified      | 0.2.7            |
| M5GFX          | 0.2.9            |
| WiFi (ESP32)   | 3.2.1            |
| PubSubClient   | 2.8              |
| ArduinoJson    | 7.4.2            |
| Preferences    | 3.2.1            |

> You can install these from Arduino Library Manager. The board package installs WiFi/Preferences automatically.

---

## Quick Start

1. **Copy** `config.example.h` → `config.h`
2. Fill in your secrets and topics:
   ```cpp
   #define WIFI_SSID "YOUR_WIFI_SSID"
   #define WIFI_PASS "YOUR_WIFI_PASSWORD"
   #define MQTT_HOST "mqtt.example.local"
   #define MQTT_PORT 1883
   // ... topics ...
   ```
3. **Open** `M5PaperS3_MQTT_Dashboard.ino` in Arduino IDE
4. Select **Board** = *M5PaperS3*, **Port**, and build options (defaults are fine)
5. **Upload** to the device

### Arduino CLI (optional)

```bash
arduino-cli compile --fqbn m5stack:esp32:m5stack_papers3 .
arduino-cli upload  --fqbn m5stack:esp32:m5stack_papers3 -p <YOUR_PORT> .
```

---

## Configuration Notes

- `config.example.h` is safe to publish on GitHub. **Do not** put secrets here.  
  On your device, rename it to `config.h` and add `config.h` to `.gitignore`.
- **Stale detection**: `STALE_MINUTES` controls when a sensor shows `ERROR`.  
  `STALE_EXCLUDE_LIST` lists indices that should not be monitored for staleness (e.g., uptime).
- **Seasonal gauges (optional)**: Define `ENABLE_SEASONAL_GAUGES` and seasonal min/max macros.  
  The sketch will apply overrides automatically in `applySeasonalOverrides()`.

---

## Robust “Rain Uptime”

- Publisher (m5atomS3Lite) sends `uptime` (hours).  
- The dashboard **persists** the last trusted value and **debounces** regressions so:
  - Mosquitto restarts / MQTT gaps **do not reset** the reading.
  - A real **power cycle of the rain device** is considered a reset.
- You still see *DRY/RAIN* as a separate boolean; **uptime is independent** from that state.

---

## Troubleshooting

### `error: 'thiLabel' was not declared in this scope`

Put a **forward declaration** above `drawTodayCard()`:

```cpp
const char* thiLabel(float thi);
```

Or move/inline the full function near the `THI_*` constants:

```cpp
static inline const char* thiLabel(float thi) {
  if (isnan(thi)) return "--";
  if (thi < THI_COOL_MAX)  return "COOL";
  if (thi < THI_COMFY_MAX) return "COMFY";
  if (thi < THI_WARM_MAX)  return "WARM";
  return "HOT";
}
```

### ArduinoJson v7 deprecation warnings

`StaticJsonDocument` is deprecated; the sketch still works.  
You may migrate to `JsonDocument` later to silence warnings.

### Re‑definition warnings (NUM_DIGITAL_PINS, etc.)

These come from the ESP32 core and the M5PaperS3 variant. They are harmless.

---

## Contributing

Issues and PRs are welcome. Please keep comments bilingual where helpful.

---

## License

MIT
