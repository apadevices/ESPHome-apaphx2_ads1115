# Examples

## phx2-poolmonitor.yaml — ESPHome device configuration

Complete ESPHome YAML configuration for an ESP32 running the `apaphx_ads1115` component with a PHX v2 board.

### What is included

- Full `apaphx_ads1115` platform configuration — pH, ORP, calibration status, soft limits, calibration age
- DS18B20 water temperature via OneWire (GPIO32)
- Analog pressure sensor via ADC (GPIO33)
- Pump status GPIO input (GPIO25) with debounce
- HA-configurable soft limit number entities
- Water Quality Score (0–100) composite sensor
- Water Quality text label (Excellent / Good / Fair / Poor / Critical)
- Filter pressure status text label
- Calibration buttons with status feedback
- WiFi, OTA, web server, diagnostics

### Before flashing

1. Create a `secrets.yaml` file in your ESPHome config directory:
   ```yaml
   wifi_ssid: "YourNetworkName"
   wifi_password: "YourNetworkPassword"
   ```
2. Replace all placeholder values in the YAML:
   - `YOUR_API_ENCRYPTION_KEY_HERE` → generate with `esphome -c . run --generate-key`
   - `YOUR_OTA_PASSWORD_HERE` → any strong password
   - `YOUR_FALLBACK_AP_PASSWORD_HERE` → any strong password
3. Adjust GPIO pin assignments if your wiring differs
4. Adjust pressure sensor `calibrate_linear` points to match your sensor datasheet

### Adapt to your hardware

| Section | What to change |
|---|---|
| `esp32.board` | Change if not using `esp32dev` |
| `i2c` pins | `sda`/`scl` GPIO if different from GPIO21/22 |
| `one_wire` pin | GPIO32 → your DS18B20 data pin |
| `adc` pin | GPIO33 → your pressure sensor pin |
| `gpio` pump pin | GPIO25 → your pump feedback pin |
| `calibrate_linear` | Adjust to your pressure sensor voltage/bar curve |
| `pin.inverted` | Match pump signal polarity to your wiring |

---

## pool-dashboard.yaml — Home Assistant dashboard

A complete 4-column pool monitoring dashboard for Home Assistant.

### Features

- Dynamic status chip bar — pH/ORP alerts, calibration age (color-coded green/orange/red), pump state
- Blinking pressure warning banner — appears automatically when filter pressure reaches 1.0 bar
- Water Quality Score card with 24h trend graph
- Live pool camera feed
- pH, ORP, temperature and pressure trend graphs (48h/24h) with soft limit reference lines
- Filter runtime audit — daily runtime counter + schedule history
- Technology control — pump, filter mode, manual vacuum, pool lighting
- Calibration panel — one-button pH and ORP calibration triggers
- System diagnostics — WiFi signal, uptime, calibration reset

### Required HACS frontend extensions

All four must be installed via HACS → Frontend before the dashboard will render:

| Extension | HACS search name | Purpose |
|---|---|---|
| **layout-card** | `layout-card` | 4-column grid layout (`custom:grid-layout`) |
| **Mushroom** | `Mushroom` | Status chips, entity cards, template cards |
| **mini-graph-card** | `mini-graph-card` | Trend graphs for pH, ORP, temperature, pressure |
| **card-mod** | `card-mod` | Custom CSS styling (chip bar, camera height, blink animation) |

### Dashboard installation

1. Install all four HACS extensions listed above and reload the browser
2. In Home Assistant go to **Settings → Dashboards → + Add Dashboard**
3. Set a title (e.g. `Bazén`) and check **"Start with an empty dashboard"**
4. Open the new dashboard → pencil icon → three dots → **Raw configuration editor**
5. Replace entire content with `pool-dashboard.yaml`
6. Click **Save**

### Entity customization

The dashboard uses entity IDs from a device named `phx2-probe`. If your device has a different name, do a find/replace of `phx2_poolmonitor` with your actual entity prefix.

Entities specific to the example setup that you will need to adapt:

| Entity in dashboard | Description |
|---|---|
| `camera.cam02_vga_bazen` | Pool camera — replace with your camera entity |
| `sensor.apa_temp` | Water temperature sensor |
| `switch.smr_bazen_pumpa` | Pool pump switch |
| `switch.a6_relay3` | Pool lighting relay |
| `input_select.filter_state_selector` | Filter mode selector |
| `input_boolean.toggle_filtrace_manual` | Manual vacuum toggle |
| `counter.pocitadlo_bazen_2h` | Daily filter runtime counter |
| `schedule.filter_timer_workdays` | Weekday filter schedule |
| `schedule.filter_timer_weekend` | Weekend filter schedule |
