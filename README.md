# ESPHome-apaphx2_ads1115

**ESPHome component for APADevices PHX v2 pool monitoring board**

pH and ORP/RX measurement via ADS1115 16-bit ADC with full galvanic isolation, guided two-point calibration, temperature compensation, soft limit alerting and water quality scoring. Full Home Assistant integration with runtime-configurable parameters.

---

## Contents

- [Features](#features)
- [Hardware Overview](#hardware-overview)
- [Wiring](#wiring)
- [Installation](#installation)
- [YAML Configuration](#yaml-configuration)
- [Calibration](#calibration)
- [Soft Limits](#soft-limits)
- [Water Quality Score](#water-quality-score)
- [Sensors Reference](#sensors-reference)
- [Troubleshooting](#troubleshooting)
- [Known Limitations](#known-limitations)
- [Changelog](#changelog)
- [License](#license)

---

## Features

- **16-bit precision** — ADS1115 delta-sigma ADC, differential AIN0+/AIN1− input
- **Full galvanic isolation** — ADUM1251 I2C isolator per channel, LMP7721 precision op-amp frontend
- **Two-point calibration** — guided A-B-C three-window stability check, 220s mandatory probe soak, 360s timeout
- **Electrode slope validation** — Nernst equation check after pH calibration (40–70 mV/pH accepted)
- **Temperature compensation** — Pasco 2001 formula, normalised to 25°C
- **Pump dependency** — measurements suppressed when pump is off, 30s stabilization after pump start
- **Signal filtering** — trimmed mean (10 samples, drop min+max) + rolling average (window 5)
- **Soft limit alerting** — configurable hi/lo thresholds for pH and ORP, hysteresis 0.1 pH / 10 mV
- **HA-configurable limits** — soft limit thresholds adjustable at runtime from Home Assistant, persisted across reboots
- **Water Quality Score** — composite 0–100 index from pH (55%) and ORP (45%)
- **Calibration status** — binary sensors per channel, calibration age tracking
- **EEPROM persistence** — calibration data survives power cycles

---

## Hardware Overview

> **Photos, all the specs and connector pinout diagrams** are available in the Arduino library hardware folder:
> https://github.com/apadevices/APAPHX2_ADS1115/tree/main/extras/hardware

The APADevices PHX v2 board provides two fully isolated electrochemical measurement channels on a single PCB.

| Parameter | pH channel | ORP/RX channel |
|---|---|---|
| ADC | ADS1115 16-bit | ADS1115 16-bit |
| I2C address | 0x49 | 0x48 |
| Gain | 2 (±2.048V, LSB=62.5μV) | 1 (±4.096V, LSB=125μV) |
| Frontend | LMP7721 precision op-amp | LMP7721 precision op-amp |
| Isolation | ADUM1251 I2C isolator | ADUM1251 I2C isolator |
| Resolution | ~947 counts/pH unit | ~8 counts/mV |
| Accuracy | ±0.002 pH (calibrated) | ±0.5 mV (calibrated) |
| Range | 0–14 pH | ±2000 mV |

### Power domains

The board has **two separate power domains** — both must be connected:

```
MCU side (logic)              Analog side (isolated)
Connector P1                  Connector CN2
─────────────────────         ──────────────────────
Vcc-MC1: 3.3V or 5V          +12V IN: external 12V DC
GND-MC1: MCU ground           12V/GND: 12V ground
SCL, SDA: I2C bus
```

> ⚠️ **Both power connections are mandatory.** The board will not function with only one supply.

> ⚠️ **The two grounds (GND-MC1 and 12V/GND) are galvanically isolated.** Do not connect them together.

### Protection

- PolyPTC fuse 500mA on 12V rail
- Schottky diode — reverse polarity protection
- TVS 15V/24.4V — transient protection
- H3 jumper — onboard 4.7kΩ I2C pullups (enable if no external pullups on bus)

---

## Wiring

### Connector P1 — IDC 2×5 pin header (2.54mm pitch)

```
P1 Pin  │ Signal    │ Connect to
────────┼───────────┼─────────────────────────────────
  10    │ GND-MC1   │ MCU GND
   9    │ Vcc-MC1   │ MCU supply (3.3V or 5V)
   8    │ SCL-MC1   │ MCU I2C SCL
   7    │ SDA-MC1   │ MCU I2C SDA
   6    │ RX-Alert  │ MCU pin (optional — ALERT from ORP ADS1115)
   5    │ pH-Alert  │ MCU pin (optional — ALERT from pH ADS1115)
   4    │ 12V GND   │ 12V supply negative
   3    │ 12V+      │ 12V supply positive
   2    │ 12V GND   │ 12V supply negative
   1    │ 12V+      │ 12V supply positive
```

Pins 1+3 and 2+4 are paired — connect both pins of each pair.

### Connector CN2 — KF2510 2-pin (alternative 12V input)

```
CN2 Pin  │ Signal  │ Connect to
─────────┼─────────┼──────────────────────
   1     │ 12V+    │ 12V supply positive
   2     │ 12V/GND │ 12V supply negative
```

Use either P1 pins 1–4 or CN2 for 12V — both connect to the same internal rail.

### Example wiring — ESP32

```
ESP32        PHX v2 P1
──────────   ──────────
GND      →   Pin 10 (GND-MC1)
3.3V     →   Pin 9  (Vcc-MC1)
GPIO22   →   Pin 8  (SCL-MC1)
GPIO21   →   Pin 7  (SDA-MC1)

12V PSU      PHX v2 CN2
──────────   ──────────
12V+     →   Pin 1
GND      →   Pin 2
```

H3 jumper: **ON** if no external I2C pullups are present on your MCU board.

---

## Installation

### 1. Copy component files

Copy the `components/apaphx_ads1115/` folder into your ESPHome config directory:

```
config/
└── esphome/
    └── components/
        └── apaphx_ads1115/
            ├── __init__.py
            ├── sensor.py
            ├── apaphx_ads1115.h
            └── apaphx_ads1115.cpp
```

### 2. Reference in YAML

```yaml
external_components:
  - source: components
    components: [apaphx_ads1115]
```

### 3. Minimum configuration

```yaml
i2c:
  sda: GPIO21
  scl: GPIO22

sensor:
  - platform: apaphx_ads1115
    name: "RAW pH Volts"
    id: phx_sensor
    address: 0x49
    gain: 2
    update_interval: 5s
    ph_sensor:
      name: "pH Value"
    orp:
      address: 0x48
      gain: 4
      orp_sensor:
        name: "ORP Value"
```

---

## YAML Configuration

### Top-level options

| Key | Type | Default | Description |
|---|---|---|---|
| `address` | i2c address | `0x49` | I2C address of pH ADS1115 |
| `gain` | enum | `2` | pH ADC gain (see gain table) |
| `update_interval` | duration | `5s` | Measurement update interval |
| `temperature_sensor` | sensor id | — | Sensor providing water temperature for pH compensation |
| `pump_sensor` | binary sensor id | — | Pump state sensor — suppresses readings when pump is off |
| `ph_low_number` | number id | — | HA number entity for pH low soft limit |
| `ph_high_number` | number id | — | HA number entity for pH high soft limit |
| `orp_low_number` | number id | — | HA number entity for ORP low soft limit |
| `orp_high_number` | number id | — | HA number entity for ORP high soft limit |

### Gain values

| YAML value | Voltage range | LSB |
|---|---|---|
| `6` | ±6.144V | 187.5μV |
| `4` | ±4.096V | 125μV ← ORP default |
| `2` | ±2.048V | 62.5μV ← pH default |
| `1` | ±1.024V | 31.25μV |

### Sub-sensors

```yaml
sensor:
  - platform: apaphx_ads1115
    name: "RAW pH Volts"        # raw differential voltage (V)
    id: phx_sensor
    address: 0x49
    gain: 2
    update_interval: 5s

    # Temperature sensor for pH compensation
    temperature_sensor: temp_status

    # Pump sensor -- suppresses readings when pump is off
    pump_sensor: pump_status

    # HA-configurable soft limits
    ph_low_number: ph_low_limit
    ph_high_number: ph_high_limit
    orp_low_number: orp_low_limit
    orp_high_number: orp_high_limit

    # pH value (calibrated, temperature compensated)
    ph_sensor:
      name: "pH Value"
      id: ph_value

    # Alert binary sensors
    ph_alert:
      name: "pH Alert"
    orp_alert:
      name: "ORP Alert"

    # Calibration status binary sensors
    ph_calibrated:
      name: "pH Calibrated"
    orp_calibrated:
      name: "ORP Calibrated"

    # ORP channel
    orp:
      address: 0x48
      gain: 4
      orp_sensor:
        name: "ORP Value"       # calibrated ORP in mV
      voltage_sensor:
        name: "RAW ORP Volts"   # raw differential voltage (V)

    # Calibration age tracking
    ph_calibration_age:
      name: "pH Calibration Age"
    orp_calibration_age:
      name: "ORP Calibration Age"
```

### HA-configurable soft limit numbers

```yaml
number:
  - platform: template
    name: "pH Low Limit"
    id: ph_low_limit
    min_value: 6.0
    max_value: 7.5
    step: 0.1
    restore_value: true
    initial_value: 7.1
    optimistic: true
    entity_category: config

  - platform: template
    name: "pH High Limit"
    id: ph_high_limit
    min_value: 7.0
    max_value: 8.0
    step: 0.1
    restore_value: true
    initial_value: 7.6
    optimistic: true
    entity_category: config

  - platform: template
    name: "ORP Low Limit"
    id: orp_low_limit
    min_value: 400
    max_value: 800
    step: 10
    restore_value: true
    initial_value: 650
    optimistic: true
    unit_of_measurement: mV
    entity_category: config

  - platform: template
    name: "ORP High Limit"
    id: orp_high_limit
    min_value: 600
    max_value: 1000
    step: 10
    restore_value: true
    initial_value: 850
    optimistic: true
    unit_of_measurement: mV
    entity_category: config
```

### Calibration buttons

```yaml
button:
  - platform: template
    name: "Calibrate pH 4.0"
    on_press:
      - calibrate_ph1:
          id: phx_sensor
          value: 4.0

  - platform: template
    name: "Calibrate pH 7.0"
    on_press:
      - calibrate_ph2:
          id: phx_sensor
          value: 7.0

  - platform: template
    name: "Calibrate ORP 475mV"
    on_press:
      - calibrate_orp1:
          id: phx_sensor    # phx_sensor is the single component instance for both pH and ORP
          value: 475

  - platform: template
    name: "Calibrate ORP 650mV"
    on_press:
      - calibrate_orp2:
          id: phx_sensor
          value: 650

  - platform: template
    name: "Reset Calibration"
    on_press:
      - reset_calibration:
          id: phx_sensor
```

---

## Calibration

Calibration is the most important step for accurate measurements. The library uses **two-point calibration** — two known reference solutions are measured and all subsequent readings are mapped between them.

### What you need

**pH calibration:**
- pH 4.0 liquid buffer solution
- pH 7.0 liquid buffer solution
- Distilled water for rinsing

**ORP calibration:**
- 475 mV ORP reference solution
- 650 mV ORP reference solution

Use liquid buffer solutions — powder-based solutions are not precise enough.

### Calibration process

Each calibration point follows this sequence automatically:

```
Place probe in buffer solution
Press calibration button in HA
         │
         ▼
[220s mandatory soak]
  soak 30s/220s...
  soak 60s/220s...
  ...
  soak 210s/220s...
         │
         ▼
[A-B-C stability loop]
  A=xxx B=xxx C=xxx |AB|=x.xxx |BC|=x.xxx |AC|=x.xxx
  drifting -- sliding window...
  A=xxx B=xxx C=xxx ...
  STABLE -- result=xxx mV
         │
         ▼
Point calibrated and saved to flash
```

The 220s soak is mandatory — pH and ORP electrodes need 60–180s to equilibrate in a new buffer solution. Starting the stability check too early produces an incorrect calibration that looks valid but gives wrong readings.

### Stability check

Three consecutive windows (A, B, C) of 50 samples each are compared:
- `|A−B| < 0.5 mV` AND `|B−C| < 0.5 mV` AND `|A−C| < 0.5 mV`

The third condition (`|A−C|`) catches slow monotonic drift that a simple two-window check would miss. If stability is not reached within 360s, the best available result is used and a warning is logged.

### Slope validation (pH only)

After pH point 2 calibration, the electrode slope is calculated and compared against the Nernst equation (59.16 mV/pH at 25°C):

| Result | Slope range | Action |
|---|---|---|
| Excellent | ≥95% of Nernst (~56–70 mV/pH) | None |
| Acceptable | 40–70 mV/pH | Monitor probe condition |
| Out of range | <40 or >70 mV/pH | Check probe and buffer solutions |

### HA disconnect during calibration

The ESP32 blocks during the 220s soak and stability loop — Home Assistant will disconnect and automatically reconnect when calibration completes. This is expected behavior and does not affect the calibration result.

### Calibration persistence

Calibration data is saved to ESP32 flash after each successful calibration point. It survives power cycles and OTA updates. The rolling average ring is automatically cleared after each calibration point to prevent stale pre-calibration values from affecting subsequent readings.

---

## Soft Limits

Soft limits trigger a binary sensor alert in Home Assistant when a reading crosses a threshold. Unlike hard clamps, soft limits do not affect the published sensor value — they only signal that attention is needed.

### Hysteresis

To prevent rapid toggling at the threshold boundary:
- Alert activates immediately when limit is crossed
- Alert clears only when value returns past limit + hysteresis
  - pH hysteresis: 0.1 pH units
  - ORP hysteresis: 10 mV

### Runtime configuration

Soft limit thresholds are exposed as `number:` entities in Home Assistant. They can be adjusted at runtime without reflashing and are persisted across reboots via `restore_value: true`.

Default values:

| Limit | Default |
|---|---|
| pH Low | 7.1 |
| pH High | 7.6 |
| ORP Low | 650 mV |
| ORP High | 850 mV |

---

## Water Quality Score

A composite 0–100 index providing an at-a-glance assessment of pool water quality.

### Calculation

```
Score = pH sub-score (0–55) + ORP sub-score (0–45)
```

**pH sub-score (55 points max):**

| pH range | Score |
|---|---|
| 7.2 – 7.6 | 55 (full) |
| 6.8 – 7.2 | Linear 0→55 |
| 7.6 – 8.0 | Linear 55→0 |
| ≤ 6.8 or ≥ 8.0 | 0 |

**ORP sub-score (45 points max):**

| ORP range | Score |
|---|---|
| ≥ 750 mV | 45 (full) |
| 400 – 750 mV | Linear 0→45 |
| ≤ 400 mV | 0 |

### Score bands

| Score | Label | Meaning |
|---|---|---|
| 90–100 | Excellent | Water perfectly balanced |
| 70–89 | Good | Minor adjustment may help |
| 50–69 | Fair | Attention needed |
| 30–49 | Poor | Action required |
| 0–29 | Critical | Immediate action required |

### Important notes

- Score requires both pH and ORP to be **calibrated** — returns 0 if either reading is invalid
- Score is calculated in YAML — no reflashing needed to adjust thresholds
- Temperature affects both pH (compensated) and ORP (not compensated) — readings at extreme temperatures may affect score accuracy

---

## Sensors Reference

| Entity | Type | Unit | Description |
|---|---|---|---|
| RAW pH Volts | Sensor | V | Raw differential voltage from pH ADC |
| pH Value | Sensor | pH | Calibrated, temperature-compensated pH |
| RAW ORP Volts | Sensor | V | Raw differential voltage from ORP ADC |
| ORP Value | Sensor | mV | Calibrated ORP/RX value |
| pH Alert | Binary sensor | — | ON when pH outside soft limits |
| ORP Alert | Binary sensor | — | ON when ORP outside soft limits |
| pH Calibrated | Binary sensor | — | ON when pH two-point calibration complete |
| ORP Calibrated | Binary sensor | — | ON when ORP two-point calibration complete |
| pH Calibration Age | Sensor | days | Days since last pH calibration |
| ORP Calibration Age | Sensor | days | Days since last ORP calibration |
| Calibration Warning | Binary sensor | — | ON when any calibration age > 40 days |
| Water Quality Score | Sensor | % | Composite water quality index 0–100 |
| Water Quality | Text sensor | — | Score band label |
| pH Low Limit | Number | pH | Configurable pH low threshold |
| pH High Limit | Number | pH | Configurable pH high threshold |
| ORP Low Limit | Number | mV | Configurable ORP low threshold |
| ORP High Limit | Number | mV | Configurable ORP high threshold |

---

## Examples

The `examples/` folder contains ready-to-use configuration files.

### phx2-poolmonitor.yaml — ESPHome device config

Complete ESPHome YAML for ESP32 + PHX v2 board. Includes all sensors, calibration buttons, Water Quality Score, pressure monitoring and pump dependency. All sensitive values replaced with clearly marked placeholders. See `examples/README.md` for setup instructions.

### pool-dashboard.yaml — Home Assistant dashboard

A complete 4-column pool monitoring dashboard featuring:
- Dynamic status chip bar — pH/ORP alerts, calibration age (color-coded), pump state
- Blinking pressure warning banner (auto-shows at ≥1.0 bar)
- Water Quality Score with 24h trend graph
- Live camera feed
- pH, ORP, temperature and pressure trend graphs
- Filter runtime audit and schedule history
- Calibration panel and system diagnostics

**Required HACS frontend extensions:**

| Extension | Purpose |
|---|---|
| [layout-card](https://github.com/thomasloven/lovelace-layout-card) | 4-column grid layout |
| [Mushroom](https://github.com/piitaya/lovelace-mushroom) | Status chips, entity and template cards |
| [mini-graph-card](https://github.com/kalkih/mini-graph-card) | Trend graphs |
| [card-mod](https://github.com/thomasloven/lovelace-card-mod) | Custom CSS styling |

See `examples/README.md` for full installation instructions and entity customization guide.

---

## Troubleshooting

### Sensors show Unknown after boot
Binary sensors (`pH Alert`, `ORP Alert`, `pH Calibrated`, `ORP Calibrated`) publish their initial state during `setup()`. If they show Unknown, check that the component loaded successfully — look for `Setting up APA PHX ADS1115...` in the ESPHome log.

### No readings appear
Check pump sensor state — if pump is configured and not running, all measurements are suppressed. Also check the 30s stabilization delay after pump starts (plus any `delayed_on` filter on the pump binary sensor).

### I2C communication failed
Verify both power supplies are connected (MCU 3.3V/5V and 12V analog side). Check that H3 jumper is enabled if no external I2C pullups are present. Run `i2c: scan: true` to confirm addresses 0x48 and 0x49 are visible.

### pH reads wrong after calibration
Check electrode slope in the ESPHome log after point 2 calibration. If slope is outside 40–70 mV/pH, the probe may be aged or contaminated, or the buffer solutions may be expired. Clean the probe and retry with fresh buffers.

### Calibration fails — no stable reading
Ensure the probe is fully submerged in the buffer solution. Allow extra time for equilibration. If timeout occurs repeatedly, the probe may need replacement.

### HA disconnects during calibration
Expected behavior — the ESP32 blocks during the 220s soak. Home Assistant reconnects automatically when calibration completes. Calibration result is unaffected.

### `took a long time (220ms)` warning in log
Expected behavior in v1.1.0. The 10-sample trimmed mean blocks for ~100ms per channel per update cycle. Will be addressed in v1.2.0 with a non-blocking state machine.

### Calibration age shows wrong value after reboot
Calibration age requires NTP time sync to be valid. The component guards against publishing garbage values before NTP sync (timestamps before year 2001 are ignored).

---

## Known Limitations

- **Blocking update cycle** — `update()` blocks ~220ms per call (10 samples × 2 channels). ESPHome logs a harmless `took a long time` warning every cycle. Planned fix: non-blocking state machine in v1.2.0.
- **Single component instance** — pH and ORP share one component instance (`id: phx_sensor`). All calibration actions reference this single id regardless of which channel is being calibrated.
- **ORP temperature compensation** — ORP readings are not temperature compensated. This is standard practice for pool monitoring but may affect accuracy at water temperatures far from 25°C.
- **Water Quality Score** — requires both pH and ORP to be calibrated. Returns 0 if either reading is NaN.

---

## Changelog

### v1.1.0 — first public release

**Core:**
- ADS1115 16-bit differential ADC implementation (AIN0+/AIN1−)
- Single-shot mode with OS-bit polling — no fixed delays
- Gain 2 for pH (±2.048V), Gain 1 for ORP (±4.096V)
- ORP range expanded to ±2000mV

**Calibration:**
- A-B-C three-window stability check (ported from APAPHX2_ADS1115 Arduino library)
- 220s mandatory probe soak with 30s heartbeat logging
- 360s timeout — returns best result, never hangs
- Watchdog feeds throughout blocking calibration
- Electrode slope validation after pH point 2 (Nernst equation)
- Rolling average cleared after each calibration point

**Measurement:**
- Trimmed mean filter — 10 samples, drop min+max, average 8
- Rolling average ring buffer — window 5, fixed size, no heap allocation
- Temperature compensation — Pasco 2001 formula, aligned with Arduino library
- Pump dependency with 30s stabilization delay

**HA integration:**
- pH Alert / ORP Alert binary sensors with configurable hysteresis
- pH Calibrated / ORP Calibrated status binary sensors
- HA-configurable soft limits via `number:` platform, persisted across reboots
- Calibration age tracking with NTP guard
- Water Quality Score (0–100) composite index
- Water Quality text label (Excellent / Good / Fair / Poor / Critical)

**Code quality:**
- Fixed: `read_byte()` → `read_byte_16()` for correct I2C presence test
- Fixed: `convert_to_orp()` returns NaN when uncalibrated
- Fixed: trimmed mean min==max edge case
- Fixed: NTP guard prevents garbage calibration age before time sync
- Fixed: rolling average cleared after calibration
- Removed: unused `TempCompensationLog` circular buffer (~480 bytes RAM freed)
- Renamed: `ADS1015_REG_CONFIG_PGA_*` → `ADS1115_REG_CONFIG_PGA_*`
- Moved: `read_voltage()` and `get_stable_reading()` to `protected`
- Removed: unused `StatusFlags` bitfield

---

## License

MIT License — see LICENSE file.

Copyright © 2025 APA Devices <kecup@vazac.eu>
