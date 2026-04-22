# Changelog

All notable changes to ESPHome-apaphx2_ads1115 are documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versioning follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.1.0] — 2025-06-01 — First public release

### Added
- ADS1115 16-bit differential ADC core (AIN0+/AIN1−, single-shot, OS-bit polling)
- Gain 2 for pH channel (±2.048V, LSB=62.5μV)
- Gain 1 for ORP channel (±4.096V, LSB=125μV, full ±2000mV range)
- A-B-C three-window stability check for calibration (ported from APAPHX2_ADS1115 Arduino library)
- 220s mandatory probe soak with 30s heartbeat logging during calibration
- 360s calibration timeout — returns best result, never hangs indefinitely
- Watchdog feeds (`App.feed_wdt()`) throughout all blocking calibration loops
- Electrode slope validation after pH point 2 — Nernst equation check (40–70 mV/pH)
- Trimmed mean filter — 10 samples per reading, drop min and max, average remaining 8
- Rolling average ring buffer — fixed window 5, no heap allocation after init
- Temperature compensation — Pasco 2001 formula, normalised to 25°C
- Pump dependency — measurements suppressed when pump is off, 30s stabilization after start
- pH Alert / ORP Alert binary sensors with hysteresis (0.1 pH / 10 mV)
- pH Calibrated / ORP Calibrated binary sensors — ON after successful two-point calibration
- HA-configurable soft limits via `number:` platform entities, persisted across reboots
- Calibration age tracking sensors (days since last calibration per channel)
- NTP guard — calibration age not published before time sync
- Water Quality Score — composite 0–100 index (pH 55% + ORP 45%)
- Water Quality text label — Excellent / Good / Fair / Poor / Critical
- Rolling average cleared automatically after each calibration point
- Initial state published for all binary sensors on boot (prevents Unknown in HA)
- Full ESPHome log output at appropriate levels throughout all operations

### Fixed
- `read_byte()` replaced with `read_byte_16()` for I2C presence test in `setup()` — prevented false failure when config register read as 0x00
- `convert_to_orp()` now returns NaN when uncalibrated — consistent with `convert_to_ph()`
- Trimmed mean min==max edge case — forces different indices when all samples identical
- Calibration age guard — no garbage values published before NTP time sync
- Rolling average not cleared after calibration — stale pre-calibration values no longer contaminate post-calibration readings

### Changed
- `ADS1015_REG_CONFIG_PGA_*` constants renamed to `ADS1115_REG_CONFIG_PGA_*`
- Temperature compensation formula updated from custom chlorinated-pool formula to Pasco 2001 — aligned with APAPHX2_ADS1115 Arduino library
- `read_voltage()` and `get_stable_reading()` moved from `public` to `protected`
- ORP hard clamp expanded from ±1000mV to ±2000mV
- Component instance id renamed from `ph_voltage` to `phx_sensor` in example YAML
- AUTO_LOAD updated to include `binary_sensor` and `number`

### Removed
- EMA (exponential moving average) filter — replaced by trimmed mean + rolling average
- Median filter — replaced by trimmed mean + rolling average
- `TempCompensationLog` circular buffer and `CircularBuffer` template — unused, freed ~480 bytes RAM
- `StatusFlags` bitfield — replaced with single `calibration_valid_` boolean
- `CONF_EMA_ALPHA` and `CONF_MEDIAN_WINDOW` YAML keys
- `CHLORINE_PH_TEMP_COEF` constant — no longer used after formula change

### Added examples
- `examples/phx2-poolmonitor.yaml` — complete sanitized ESPHome device config with full comments, placeholder credentials, GPIO annotations and calibration procedure notes
- `examples/pool-dashboard.yaml` — complete HA dashboard with layout-card grid, Mushroom cards, mini-graph-card trends, card-mod styling
- `examples/README.md` — setup guide covering both files: GPIO wiring, secrets, HACS extensions, installation steps, entity customization

### Known limitations
- `update()` blocks ~220ms per call for trimmed mean sampling (10 samples × 2 channels). ESPHome logs a harmless `took a long time` warning. Planned fix: non-blocking state machine in v1.2.0.

---

## [Unreleased]

### Planned for v1.2.0
- Non-blocking state machine for measurement sampling
- Free chlorine estimation from ORP + pH + temperature
- Recommended action text sensor
- Additional Water Quality Score refinements

---

[1.1.0]: https://github.com/apadevices/ESPHome-apaphx2_ads1115/releases/tag/v1.1.0
