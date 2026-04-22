#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/core/preferences.h"
#include "esphome/core/automation.h"
#include "esphome/core/time.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"

namespace esphome {
namespace apaphx_ads1115 {

static const char *const TAG = "apaphx.ads1115";

// ============================================================
// ADS1115 register pointers
// ============================================================
static const uint8_t  ADS1115_REG_POINTER_CONVERT         = 0x00;
static const uint8_t  ADS1115_REG_POINTER_CONFIG          = 0x01;

// ============================================================
// Config register -- OS bit
// ============================================================
static const uint16_t ADS1115_REG_CONFIG_OS_SINGLE        = 0x8000;  // Write: start single-shot conversion
static const uint16_t ADS1115_REG_CONFIG_OS_READY         = 0x8000;  // Read:  1 = conversion complete

// ============================================================
// Config register -- MUX
// PHX v2 uses differential AIN0(+)/AIN1(-):
//   AIN0 = 2.5V offset reference (probe analog-GND)
//   AIN1 = LMP7721 op-amp output (probe signal + 2.5V offset)
//   Result = AIN0 - AIN1 = -probe_signal (sign inverted by hardware)
//   Two-point calibration math compensates for sign -- no user action needed.
// ============================================================
static const uint16_t ADS1115_REG_CONFIG_MUX_DIFF_0_1     = 0x0000;  // Differential AIN0+/AIN1- (PHX v2 default)

// ============================================================
// Config register -- MODE
// ============================================================
static const uint16_t ADS1115_REG_CONFIG_MODE_SINGLESHOT  = 0x0100;  // Single-shot, power-down after conversion

// ============================================================
// Config register -- DR (data rate / samples per second)
// 128 SPS = ~7.8ms per conversion.
// Note: 0x0080 is the same bit pattern as ADS1015 DR_1600SPS -- renamed for clarity.
// ============================================================
static const uint16_t ADS1115_REG_CONFIG_DR_128SPS        = 0x0080;  // 128 SPS (default)

// ============================================================
// Config register -- COMP_QUE
// Disable comparator, set ALERT pin to high-Z
// ============================================================
static const uint16_t ADS1115_REG_CONFIG_COMP_QUE_DISABLE = 0x0003;

// ============================================================
// Gain settings (PGA) -- register bit values identical to ADS1015
//   pH  channel: Gain 2 (+/-2.048V, LSB = 62.5uV)  -- differential signal +/-~500mV
//   ORP channel: Gain 1 (+/-4.096V, LSB = 125uV)   -- ORP full range +/-2000mV
// ============================================================
static const uint16_t ADS1115_REG_CONFIG_PGA_6_144V       = 0x0000;  // +/-6.144V  LSB=187.5uV
static const uint16_t ADS1115_REG_CONFIG_PGA_4_096V       = 0x0200;  // +/-4.096V  LSB=125uV   (ORP default)
static const uint16_t ADS1115_REG_CONFIG_PGA_2_048V       = 0x0400;  // +/-2.048V  LSB=62.5uV  (pH default)
static const uint16_t ADS1115_REG_CONFIG_PGA_1_024V       = 0x0600;  // +/-1.024V  LSB=31.25uV
static const uint16_t ADS1115_REG_CONFIG_PGA_0_512V       = 0x0800;  // +/-0.512V  LSB=15.625uV
static const uint16_t ADS1115_REG_CONFIG_PGA_0_256V       = 0x0A00;  // +/-0.256V  LSB=7.8125uV

// ============================================================
// Calibration configuration -- A-B-C three-window stability approach
// ============================================================
static const uint8_t  CAL_SAMPLES         = 50;        // samples per calibration window (~500ms per window)
static const uint32_t SAMPLE_DELAY_MS     = 10;        // inter-sample delay (ms)
static const uint32_t PORTION_DELAY_MS    = 500;       // pause between windows (ms)
static const float    STABILITY_THRESHOLD = 0.5f;      // per-window threshold (mV) -- |A-B|, |B-C|, |A-C| < 0.5mV
static const uint32_t CAL_SOAK_MS         = 220000UL;  // mandatory probe soak before stability loop (220s)
                                                        // pH/ORP electrodes need 60-180s to equilibrate
static const uint32_t CAL_TIMEOUT_MS      = 360000UL;  // max stability loop time (360s = 6 min)
                                                        // on timeout: returns best result + logs warning

// ============================================================
// Filter configuration
// Trimmed mean: collect RAW_SAMPLE_COUNT samples, drop min+max, average remaining
// Rolling average: window of ROLLING_AVG_WINDOW readings
// ============================================================
static const uint8_t  RAW_SAMPLE_COUNT    = 10;   // samples per reading cycle
static const uint8_t  ROLLING_AVG_WINDOW  = 5;    // rolling average ring size

// ============================================================
// Soft limit hysteresis
// Applied when clearing alerts -- prevents rapid on/off toggling at threshold
// ============================================================
static constexpr float PH_HYSTERESIS  = 0.1f;   // pH units
static constexpr float ORP_HYSTERESIS = 10.0f;  // mV

// ============================================================
// Temperature compensation constants (Pasco 2001 formula)
// ============================================================
static constexpr float REFERENCE_TEMP_C = 25.0f;  // reference temperature in degC
static constexpr float NEUTRAL_PH       = 7.0f;   // neutral point for Pasco formula

// ============================================================
// Pump stabilization time
// ============================================================
static constexpr uint32_t PUMP_STABILIZE_TIME = 30000;  // 30 seconds in ms

// ============================================================
// OS-bit polling timeout for single-shot conversion
// At 128 SPS conversion takes ~7.8ms -- 50ms is a safe timeout
// ============================================================
static constexpr uint32_t ADC_CONVERSION_TIMEOUT_MS = 50;

struct CalibrationData {
    float ph_ref1_mv;
    float ph_ref2_mv;
    float ph_ref1_value;
    float ph_ref2_value;
    uint32_t ph_last_calibration;

    float orp_ref1_mv;
    float orp_ref2_mv;
    float orp_ref1_value;
    float orp_ref2_value;
    uint32_t orp_last_calibration;
};

class APAPHX_ADS1115 : public sensor::Sensor, public PollingComponent, public i2c::I2CDevice {
 public:
    APAPHX_ADS1115();

    void setup() override;
    void dump_config() override;
    void update() override;

    // Public conversion functions (useful for diagnostics)
    float convert_to_ph(float voltage);
    float convert_to_orp(float voltage);

    // Configuration setters
    void set_ph_gain(uint16_t gain) { ph_gain_ = gain; }
    void set_orp_gain(uint16_t gain) { orp_gain_ = gain; }
    void set_ph_address(uint8_t address) { ph_address_ = address; }
    void set_orp_address(uint8_t address) { orp_address_ = address; }
    void set_ph_sensor(sensor::Sensor *sens) { ph_sensor_ = sens; }
    void set_orp_sensor(sensor::Sensor *sens) { orp_sensor_ = sens; }
    void set_orp_voltage_sensor(sensor::Sensor *sens) { orp_voltage_sensor_ = sens; }
    void set_ph_cal_age_sensor(sensor::Sensor *sens) { ph_cal_age_sensor_ = sens; }
    void set_orp_cal_age_sensor(sensor::Sensor *sens) { orp_cal_age_sensor_ = sens; }
    void set_temperature_sensor(sensor::Sensor *temp_sensor) { temp_sensor_ = temp_sensor; }
    void set_pump_sensor(binary_sensor::BinarySensor *pump_sensor) { pump_sensor_ = pump_sensor; }
    void set_ph_alert_sensor(binary_sensor::BinarySensor *s) { ph_alert_sensor_ = s; }
    void set_orp_alert_sensor(binary_sensor::BinarySensor *s) { orp_alert_sensor_ = s; }
    void set_ph_calibrated_sensor(binary_sensor::BinarySensor *s) { ph_calibrated_sensor_ = s; }
    void set_orp_calibrated_sensor(binary_sensor::BinarySensor *s) { orp_calibrated_sensor_ = s; }
    void set_ph_low_number(number::Number *n)  { ph_low_number_  = n; }
    void set_ph_high_number(number::Number *n) { ph_high_number_ = n; }
    void set_orp_low_number(number::Number *n)  { orp_low_number_  = n; }
    void set_orp_high_number(number::Number *n) { orp_high_number_ = n; }

    // Calibration functions
    void calibrate_ph_point1(float ph_value);
    void calibrate_ph_point2(float ph_value);
    void calibrate_orp_point1(float mv_value);
    void calibrate_orp_point2(float mv_value);
    void reset_calibration();

 protected:
    void load_calibration_();
    void save_calibration_();
    void update_calibration_age_();

    // ADC core -- protected, use get_filtered_reading_() from update()
    float read_voltage(uint8_t address, uint16_t gain);
    float get_stable_reading(uint8_t address, uint16_t gain);

    // Filtering helpers
    float collect_trimmed_mean_(uint8_t address, uint16_t gain);
    float get_filtered_reading_(uint8_t address, uint16_t gain);

    // Soft limit helpers
    void check_ph_limits_(float ph_value);
    void check_orp_limits_(float orp_value);
    float compensate_ph_for_temperature_(float ph, float temp_c);
    void report_memory_status_();

    // Member variables
    // pH: Gain 2 (+/-2.048V, LSB=62.5uV) -- differential signal +/-~500mV fits this range
    // ORP: Gain 1 (+/-4.096V, LSB=125uV) -- full +/-2000mV range requires Gain 1
    uint16_t ph_gain_{ADS1115_REG_CONFIG_PGA_2_048V};
    uint16_t orp_gain_{ADS1115_REG_CONFIG_PGA_4_096V};
    uint8_t ph_address_{0x49};
    uint8_t orp_address_{0x48};
    sensor::Sensor *ph_sensor_{nullptr};
    sensor::Sensor *orp_sensor_{nullptr};
    sensor::Sensor *orp_voltage_sensor_{nullptr};
    sensor::Sensor *ph_cal_age_sensor_{nullptr};
    sensor::Sensor *orp_cal_age_sensor_{nullptr};
    sensor::Sensor *temp_sensor_{nullptr};
    binary_sensor::BinarySensor *pump_sensor_{nullptr};
    ESPPreferenceObject pref_;
    CalibrationData cal_data_{0, 0, 4.0f, 7.0f, 0, 0, 0, 475.0f, 650.0f, 0};

    // Soft limit sensors and HA-configurable number entities
    binary_sensor::BinarySensor *ph_alert_sensor_{nullptr};
    binary_sensor::BinarySensor *orp_alert_sensor_{nullptr};
    // Calibration status binary sensors -- ON = calibrated, OFF = not calibrated
    binary_sensor::BinarySensor *ph_calibrated_sensor_{nullptr};
    binary_sensor::BinarySensor *orp_calibrated_sensor_{nullptr};
    number::Number *ph_low_number_{nullptr};
    number::Number *ph_high_number_{nullptr};
    number::Number *orp_low_number_{nullptr};
    number::Number *orp_high_number_{nullptr};
    bool ph_alert_active_{false};
    bool orp_alert_active_{false};

    // Rolling average ring buffers -- fixed size, no heap after init
    float ph_avg_ring_[ROLLING_AVG_WINDOW]{};
    float orp_avg_ring_[ROLLING_AVG_WINDOW]{};
    uint8_t ph_avg_index_{0};
    uint8_t orp_avg_index_{0};
    uint8_t ph_avg_filled_{0};
    uint8_t orp_avg_filled_{0};

    // Pump control variables
    bool was_pump_running_{false};
    uint32_t pump_start_time_{0};

    // Calibration validity flag -- set after successful calibration, cleared on reset
    bool calibration_valid_{false};
};

// ============================================================
// Action classes
// ============================================================
template<typename... Ts> class CalibratePh1Action : public Action<Ts...> {
 public:
  explicit CalibratePh1Action(APAPHX_ADS1115 *parent, float value) : parent_(parent), value_(value) {}
  void play(Ts... x) override { this->parent_->calibrate_ph_point1(this->value_); }
 protected:
  APAPHX_ADS1115 *parent_;
  float value_;
};

template<typename... Ts> class CalibratePh2Action : public Action<Ts...> {
 public:
  explicit CalibratePh2Action(APAPHX_ADS1115 *parent, float value) : parent_(parent), value_(value) {}
  void play(Ts... x) override { this->parent_->calibrate_ph_point2(this->value_); }
 protected:
  APAPHX_ADS1115 *parent_;
  float value_;
};

template<typename... Ts> class CalibrateOrp1Action : public Action<Ts...> {
 public:
  explicit CalibrateOrp1Action(APAPHX_ADS1115 *parent, float value) : parent_(parent), value_(value) {}
  void play(Ts... x) override { this->parent_->calibrate_orp_point1(this->value_); }
 protected:
  APAPHX_ADS1115 *parent_;
  float value_;
};

template<typename... Ts> class CalibrateOrp2Action : public Action<Ts...> {
 public:
  explicit CalibrateOrp2Action(APAPHX_ADS1115 *parent, float value) : parent_(parent), value_(value) {}
  void play(Ts... x) override { this->parent_->calibrate_orp_point2(this->value_); }
 protected:
  APAPHX_ADS1115 *parent_;
  float value_;
};

template<typename... Ts> class ResetCalibrationAction : public Action<Ts...> {
 public:
  explicit ResetCalibrationAction(APAPHX_ADS1115 *parent) : parent_(parent) {}
  void play(Ts... x) override { this->parent_->reset_calibration(); }
 protected:
  APAPHX_ADS1115 *parent_;
};

}  // namespace apaphx_ads1115
}  // namespace esphome
