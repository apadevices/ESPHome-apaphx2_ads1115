#include "apaphx_ads1115.h"
#include "esphome/core/hal.h"
#include "esphome/core/application.h"
#include <algorithm>

namespace esphome {
namespace apaphx_ads1115 {

APAPHX_ADS1115::APAPHX_ADS1115()
    : sensor::Sensor(), PollingComponent(5000), i2c::I2CDevice() {
    // Fixed-size ring buffers initialised in header -- no heap allocation needed
}

void APAPHX_ADS1115::setup() {
    ESP_LOGCONFIG(TAG, "Setting up APA PHX ADS1115...");
    this->pref_ = global_preferences->make_preference<CalibrationData>(879453);
    load_calibration_();

    calibration_valid_ = false;

    // Test both ADCs -- use read_byte_16() and check return value, not register content
    // read_byte() returns the byte value, not a success bool -- would false-fail if reg=0x00
    uint16_t test_val;
    this->set_i2c_address(ph_address_);
    if (!this->read_byte_16(ADS1115_REG_POINTER_CONFIG, &test_val)) {
        ESP_LOGE(TAG, "pH ADC not found at 0x%02X", ph_address_);
        this->mark_failed();
        return;
    }

    this->set_i2c_address(orp_address_);
    if (!this->read_byte_16(ADS1115_REG_POINTER_CONFIG, &test_val)) {
        ESP_LOGE(TAG, "ORP ADC not found at 0x%02X", orp_address_);
        this->mark_failed();
        return;
    }

    // Sensor initialized successfully
    report_memory_status_();

    // Publish initial state for alert and calibration binary sensors -- prevents "Unknown" in HA on boot
    if (ph_alert_sensor_       != nullptr) ph_alert_sensor_->publish_state(false);
    if (orp_alert_sensor_      != nullptr) orp_alert_sensor_->publish_state(false);
    if (ph_calibrated_sensor_  != nullptr)
        ph_calibrated_sensor_->publish_state(cal_data_.ph_last_calibration > 0);
    if (orp_calibrated_sensor_ != nullptr)
        orp_calibrated_sensor_->publish_state(cal_data_.orp_last_calibration > 0);
}

void APAPHX_ADS1115::dump_config() {
    ESP_LOGCONFIG(TAG, "APA PHX ADS1115:");
    ESP_LOGCONFIG(TAG, "  pH  ADC: address=0x%02X gain=0x%04X", this->ph_address_, this->ph_gain_);
    ESP_LOGCONFIG(TAG, "  ORP ADC: address=0x%02X gain=0x%04X", this->orp_address_, this->orp_gain_);
    ESP_LOGCONFIG(TAG, "  MUX: differential AIN0+/AIN1-");
    ESP_LOGCONFIG(TAG, "  Temperature Compensation: %s",
                 this->temp_sensor_ != nullptr ? "Yes" : "No");
    ESP_LOGCONFIG(TAG, "  Pump Control: %s",
                 this->pump_sensor_ != nullptr ? "Yes" : "No");

    time_t now = ::time(nullptr);
    if (cal_data_.ph_last_calibration > 0) {
        ESP_LOGCONFIG(TAG, "  pH Calibration: %u days old",
                     (now - cal_data_.ph_last_calibration) / 86400);
    } else {
        ESP_LOGCONFIG(TAG, "  pH Calibration: not calibrated");
    }
    if (cal_data_.orp_last_calibration > 0) {
        ESP_LOGCONFIG(TAG, "  ORP Calibration: %u days old",
                     (now - cal_data_.orp_last_calibration) / 86400);
    } else {
        ESP_LOGCONFIG(TAG, "  ORP Calibration: not calibrated");
    }
    LOG_I2C_DEVICE(this);
    if (this->is_failed()) {
        ESP_LOGE(TAG, "Communication failed!");
    }
}

void APAPHX_ADS1115::report_memory_status_() {
    ESP_LOGD(TAG, "Memory Status:");
    ESP_LOGD(TAG, "  Filter: trimmed mean (%u samples) + rolling avg (window=%u)",
             RAW_SAMPLE_COUNT, ROLLING_AVG_WINDOW);
    ESP_LOGD(TAG, "  pH avg ring: %u/%u filled", ph_avg_filled_, ROLLING_AVG_WINDOW);
    ESP_LOGD(TAG, "  ORP avg ring: %u/%u filled", orp_avg_filled_, ROLLING_AVG_WINDOW);
}

// ============================================================
// read_voltage() -- ADS1115 core
//
// Changes from ADS1015 version:
//   - raw_adc is int16_t (signed -- ORP can be negative)
//   - No >> 4 bit-shift (ADS1115 is full 16-bit)
//   - Divisor 32768.0f (2^15) instead of 2048.0f (2^11)
//   - MUX: differential AIN0+/AIN1- (0x0000) not single-ended
//   - Mode: single-shot (0x0100) not continuous
//   - OS-bit polling instead of fixed delay(1)
//   - COMP_QUE disabled (0x0003)
//   - channel parameter removed -- always differential
// ============================================================
float APAPHX_ADS1115::read_voltage(uint8_t address, uint16_t gain) {
    this->set_i2c_address(address);

    // Build config register word
    uint16_t config = ADS1115_REG_CONFIG_OS_SINGLE        |  // start conversion
                      ADS1115_REG_CONFIG_MUX_DIFF_0_1     |  // differential AIN0+/AIN1-
                      gain                                 |  // PGA gain
                      ADS1115_REG_CONFIG_MODE_SINGLESHOT   |  // single-shot mode
                      ADS1115_REG_CONFIG_DR_128SPS         |  // 128 SPS ~7.8ms/conversion
                      ADS1115_REG_CONFIG_COMP_QUE_DISABLE;    // disable comparator

    if (!write_byte_16(ADS1115_REG_POINTER_CONFIG, config)) {
        ESP_LOGW(TAG, "read_voltage: I2C write failed at 0x%02X", address);
        return NAN;
    }

    // Dummy read -- allows OS bit to clear before polling starts
    uint16_t dummy;
    read_byte_16(ADS1115_REG_POINTER_CONFIG, &dummy);

    // Poll OS bit until conversion complete or timeout
    // At 128 SPS conversion takes ~7.8ms -- timeout at 50ms is safe
    uint32_t start = millis();
    uint16_t status;
    while (true) {
        if (!read_byte_16(ADS1115_REG_POINTER_CONFIG, &status)) {
            ESP_LOGW(TAG, "read_voltage: I2C read failed during OS poll at 0x%02X", address);
            return NAN;
        }
        if (status & ADS1115_REG_CONFIG_OS_READY) break;  // conversion complete
        if ((millis() - start) >= ADC_CONVERSION_TIMEOUT_MS) {
            ESP_LOGW(TAG, "read_voltage: conversion timeout at 0x%02X", address);
            return NAN;
        }
        // Small yield on ESP -- feeds watchdog, compiles to nothing on AVR
        yield();
    }

    // Read conversion result -- signed 16-bit, no shift needed
    uint16_t raw_u;
    if (!read_byte_16(ADS1115_REG_POINTER_CONVERT, &raw_u)) {
        ESP_LOGW(TAG, "read_voltage: I2C read failed on conversion register at 0x%02X", address);
        return NAN;
    }
    int16_t raw_adc = (int16_t)raw_u;

    // Map gain constant to full-scale voltage range
    float voltage_range;
    switch (gain) {
        case ADS1115_REG_CONFIG_PGA_6_144V: voltage_range = 6.144f; break;
        case ADS1115_REG_CONFIG_PGA_4_096V: voltage_range = 4.096f; break;
        case ADS1115_REG_CONFIG_PGA_2_048V: voltage_range = 2.048f; break;
        case ADS1115_REG_CONFIG_PGA_1_024V: voltage_range = 1.024f; break;
        case ADS1115_REG_CONFIG_PGA_0_512V: voltage_range = 0.512f; break;
        case ADS1115_REG_CONFIG_PGA_0_256V: voltage_range = 0.256f; break;
        default: voltage_range = 4.096f;
    }

    // Convert raw counts to volts
    // ADS1115: 16-bit signed, full scale = 32768 counts
    return (float)raw_adc * voltage_range / 32768.0f;
}

// ============================================================
// Filtering
//
// Two-stage pipeline per reading:
//   Stage 1 -- Trimmed mean:
//     Collect RAW_SAMPLE_COUNT (10) raw ADC samples
//     Drop the single lowest and single highest value
//     Average the remaining 8 -- rejects spike outliers
//
//   Stage 2 -- Rolling average:
//     Push trimmed mean result into a ring buffer (window=5)
//     Output is the mean of all filled slots
//     Provides smooth published value without lag buildup
// ============================================================

float APAPHX_ADS1115::collect_trimmed_mean_(uint8_t address, uint16_t gain) {
    float samples[RAW_SAMPLE_COUNT];
    uint8_t valid = 0;

    // Collect RAW_SAMPLE_COUNT samples
    for (uint8_t i = 0; i < RAW_SAMPLE_COUNT; i++) {
        float v = read_voltage(address, gain);
        if (!std::isnan(v)) {
            samples[valid++] = v;
        }
    }

    if (valid == 0) return NAN;
    if (valid == 1) return samples[0];
    if (valid == 2) return (samples[0] + samples[1]) / 2.0f;

    // Find min and max indices
    uint8_t min_idx = 0, max_idx = 0;
    for (uint8_t i = 1; i < valid; i++) {
        if (samples[i] < samples[min_idx]) min_idx = i;
        if (samples[i] > samples[max_idx]) max_idx = i;
    }

    // Edge case: if all samples identical, min and max point to same index
    // Force them apart so we skip exactly two samples, not one
    if (min_idx == max_idx && valid > 2) {
        max_idx = (min_idx == 0) ? 1 : 0;
    }

    // Average remaining samples (skip min and max)
    float sum = 0.0f;
    uint8_t count = 0;
    for (uint8_t i = 0; i < valid; i++) {
        if (i == min_idx || i == max_idx) continue;
        sum += samples[i];
        count++;
    }

    return (count > 0) ? (sum / count) : samples[0];
}

float APAPHX_ADS1115::get_filtered_reading_(uint8_t address, uint16_t gain) {
    // Stage 1 -- trimmed mean
    float trimmed = collect_trimmed_mean_(address, gain);
    if (std::isnan(trimmed)) return NAN;

    // Stage 2 -- rolling average ring
    bool is_ph = (address == ph_address_);
    float *ring   = is_ph ? ph_avg_ring_   : orp_avg_ring_;
    uint8_t &idx  = is_ph ? ph_avg_index_  : orp_avg_index_;
    uint8_t &filled = is_ph ? ph_avg_filled_ : orp_avg_filled_;

    ring[idx] = trimmed;
    idx = (idx + 1) % ROLLING_AVG_WINDOW;
    if (filled < ROLLING_AVG_WINDOW) filled++;

    float sum = 0.0f;
    for (uint8_t i = 0; i < filled; i++) {
        sum += ring[i];
    }
    return sum / filled;
}

// ============================================================
// Temperature compensation -- Pasco 2001 formula
// Normalises pH reading to 25 degrees C reference temperature.
// Matches the formula used in the APAPHX2_ADS1115 Arduino library.
// pH_comp = ((pH_raw - 7) * (273.15 + T)) / (273.15 + 25) + 7
// ============================================================
float APAPHX_ADS1115::compensate_ph_for_temperature_(float ph_25c, float temp_c) {
    float compensated_ph = ((ph_25c - NEUTRAL_PH) * (273.15f + temp_c)) /
                           (273.15f + REFERENCE_TEMP_C) + NEUTRAL_PH;

    compensated_ph = std::min(std::max(compensated_ph, 0.0f), 14.0f);

    float compensation_magnitude = compensated_ph - ph_25c;
    if (compensation_magnitude < 0.0f) compensation_magnitude = -compensation_magnitude;
    if (compensation_magnitude > 0.2f) {
        ESP_LOGW(TAG, "Large pH temp compensation: %.3f -> %.3f pH (%.1f degC)",
                 ph_25c, compensated_ph, temp_c);
    } else {
        ESP_LOGD(TAG, "pH temp compensation: %.3f -> %.3f pH (%.1f degC)",
                 ph_25c, compensated_ph, temp_c);
    }

    return compensated_ph;
}

// ============================================================
// get_stable_reading() -- blocking, calibration only
//
// Process (ported from APAPHX2_ADS1115 Arduino library):
//   1. Mandatory 200s soak -- probe must equilibrate in buffer solution
//      pH/ORP electrodes need 60-180s to reach stable potential
//   2. Capture initial window A
//   3. Three-window stability loop (A, B, C):
//      Each window = CAL_SAMPLES x SAMPLE_DELAY_MS (~500ms)
//      Pause PORTION_DELAY_MS between windows
//      Check: |A-B|, |B-C|, |A-C| all < STABILITY_THRESHOLD (0.5mV)
//      Third condition catches slow monotonic drift A->B->C
//      C slides into next A -- no wasted work
//   4. Time-based timeout CAL_TIMEOUT_MS (360s)
//   5. On timeout: return best result + log warning (not NAN)
// ============================================================
float APAPHX_ADS1115::get_stable_reading(uint8_t address, uint16_t gain) {

    // Helper lambda -- collect one window of CAL_SAMPLES readings, return mean mV
    // Uses direct read_voltage() -- bypasses EMA/median filter for raw calibration mV
    auto collect_window = [&]() -> float {
        float sum = 0.0f;
        int valid = 0;
        for (int i = 0; i < CAL_SAMPLES; i++) {
            float v = read_voltage(address, gain);
            if (!std::isnan(v)) {
                sum += v * 1000.0f;  // store as mV
                valid++;
            }
            delay(SAMPLE_DELAY_MS);
            App.feed_wdt();
        }
        return (valid > 0) ? (sum / valid) : NAN;
    };

    // Step 1 -- mandatory soak with 30s heartbeat
    ESP_LOGI(TAG, "Calibration: probe soak started -- waiting 200s...");
    ESP_LOGI(TAG, "Calibration: keep probe in buffer solution, do not disturb");
    uint32_t total_soak = CAL_SOAK_MS / 1000UL;
    for (uint32_t s = 0; s < total_soak; s++) {
        // Use 100ms slices to keep watchdog fed throughout the soak
        for (int ms = 0; ms < 10; ms++) {
            delay(100);
            App.feed_wdt();
        }
        // Heartbeat every 30s so user knows device is alive
        if ((s + 1) % 30 == 0) {
            ESP_LOGI(TAG, "Calibration: soak %us/%us...", (unsigned)(s + 1), (unsigned)total_soak);
            App.feed_wdt();
        }
    }
    ESP_LOGI(TAG, "Calibration: soak complete -- starting stability loop");

    // Step 2 -- capture initial window A
    float winA = collect_window();
    if (std::isnan(winA)) {
        ESP_LOGE(TAG, "Calibration: failed to collect window A");
        return NAN;
    }
    float bestMV = winA;
    for (int _ms = 0; _ms < (int)(PORTION_DELAY_MS / 100); _ms++) { delay(100); App.feed_wdt(); }

    // Step 3 -- stability loop with timeout
    uint32_t loop_start = millis();

    while ((millis() - loop_start) < CAL_TIMEOUT_MS) {

        // Window B
        float winB = collect_window();
        if (std::isnan(winB)) { ESP_LOGW(TAG, "Calibration: window B failed, retrying"); continue; }
        for (int _ms = 0; _ms < (int)(PORTION_DELAY_MS / 100); _ms++) { delay(100); App.feed_wdt(); }

        // Window C
        float winC = collect_window();
        if (std::isnan(winC)) { ESP_LOGW(TAG, "Calibration: window C failed, retrying"); continue; }

        // Compute absolute differences -- inline abs, no math.h
        float diffAB = winA - winB; if (diffAB < 0.0f) diffAB = -diffAB;
        float diffBC = winB - winC; if (diffBC < 0.0f) diffBC = -diffBC;
        float diffAC = winA - winC; if (diffAC < 0.0f) diffAC = -diffAC;

        bestMV = (winA + winB + winC) / 3.0f;

        ESP_LOGI(TAG, "Calibration: A=%.3fmV B=%.3fmV C=%.3fmV |AB|=%.3f |BC|=%.3f |AC|=%.3f (thr=%.1f)",
                 winA, winB, winC, diffAB, diffBC, diffAC, STABILITY_THRESHOLD);

        if (diffAB < STABILITY_THRESHOLD &&
            diffBC < STABILITY_THRESHOLD &&
            diffAC < STABILITY_THRESHOLD) {
            ESP_LOGI(TAG, "Calibration: STABLE -- result=%.3f mV", bestMV);
            return bestMV / 1000.0f;  // return as volts, consistent with read_voltage()
        }

        ESP_LOGI(TAG, "Calibration: drifting -- sliding window, continuing...");

        // C slides into next A -- no wasted work
        winA = winC;
        for (int _ms = 0; _ms < (int)(PORTION_DELAY_MS / 100); _ms++) { delay(100); App.feed_wdt(); }
    }

    // Timeout -- return best result with warning
    ESP_LOGW(TAG, "Calibration: timeout after 360s -- using best result=%.3f mV", bestMV);
    return bestMV / 1000.0f;
}

// ============================================================
// Soft limit checks -- with hysteresis to prevent rapid toggling
//
// Alert activates when value crosses limit.
// Alert deactivates only when value returns past limit + hysteresis.
// This prevents bouncing at the threshold boundary.
// ============================================================

void APAPHX_ADS1115::check_ph_limits_(float ph_value) {
    if (ph_alert_sensor_ == nullptr) return;
    if (std::isnan(ph_value)) return;

    // Read current limits from HA number entities -- NAN if not configured
    float ph_low  = (ph_low_number_  != nullptr) ? ph_low_number_->state  : NAN;
    float ph_high = (ph_high_number_ != nullptr) ? ph_high_number_->state : NAN;

    // If no limits configured at all -- nothing to check
    if (std::isnan(ph_low) && std::isnan(ph_high)) return;

    bool alert = ph_alert_active_;

    if (!ph_alert_active_) {
        if ((!std::isnan(ph_low)  && ph_value < ph_low) ||
            (!std::isnan(ph_high) && ph_value > ph_high)) {
            alert = true;
            ESP_LOGW(TAG, "pH soft limit breached: %.2f (low=%.2f high=%.2f)",
                     ph_value,
                     std::isnan(ph_low)  ? -99.0f : ph_low,
                     std::isnan(ph_high) ? -99.0f : ph_high);
        }
    } else {
        // Clear only when value returns within limits + hysteresis
        bool low_ok  = std::isnan(ph_low)  || ph_value >= (ph_low  + PH_HYSTERESIS);
        bool high_ok = std::isnan(ph_high) || ph_value <= (ph_high - PH_HYSTERESIS);
        if (low_ok && high_ok) {
            alert = false;
            ESP_LOGI(TAG, "pH returned within limits: %.2f", ph_value);
        }
    }

    if (alert != ph_alert_active_) {
        ph_alert_active_ = alert;
        ph_alert_sensor_->publish_state(alert);
    }
}

void APAPHX_ADS1115::check_orp_limits_(float orp_value) {
    if (orp_alert_sensor_ == nullptr) return;
    if (std::isnan(orp_value)) return;

    // Read current limits from HA number entities -- NAN if not configured
    float orp_low  = (orp_low_number_  != nullptr) ? orp_low_number_->state  : NAN;
    float orp_high = (orp_high_number_ != nullptr) ? orp_high_number_->state : NAN;

    // If no limits configured at all -- nothing to check
    if (std::isnan(orp_low) && std::isnan(orp_high)) return;

    bool alert = orp_alert_active_;

    if (!orp_alert_active_) {
        if ((!std::isnan(orp_low)  && orp_value < orp_low) ||
            (!std::isnan(orp_high) && orp_value > orp_high)) {
            alert = true;
            ESP_LOGW(TAG, "ORP soft limit breached: %.1f mV (low=%.1f high=%.1f)",
                     orp_value,
                     std::isnan(orp_low)  ? -9999.0f : orp_low,
                     std::isnan(orp_high) ? -9999.0f : orp_high);
        }
    } else {
        // Clear only when value returns within limits + hysteresis
        bool low_ok  = std::isnan(orp_low)  || orp_value >= (orp_low  + ORP_HYSTERESIS);
        bool high_ok = std::isnan(orp_high) || orp_value <= (orp_high - ORP_HYSTERESIS);
        if (low_ok && high_ok) {
            alert = false;
            ESP_LOGI(TAG, "ORP returned within limits: %.1f mV", orp_value);
        }
    }

    if (alert != orp_alert_active_) {
        orp_alert_active_ = alert;
        orp_alert_sensor_->publish_state(alert);
    }
}

// ============================================================
// update() -- main measurement loop
// ============================================================
void APAPHX_ADS1115::update() {
    // Check pump status
    if (pump_sensor_ != nullptr) {
        if (!pump_sensor_->state) {
            if (was_pump_running_) {
                ESP_LOGD(TAG, "Pump stopped - suspending measurements");
                // Clear rolling average rings -- stale values from pump-off period
                ph_avg_filled_  = 0; ph_avg_index_  = 0;
                orp_avg_filled_ = 0; orp_avg_index_ = 0;
                was_pump_running_ = false;
            }
            return;
        } else if (!was_pump_running_) {
            pump_start_time_ = millis();
            was_pump_running_ = true;
            ESP_LOGD(TAG, "Pump started - waiting 30s before measurements");
            return;
        } else if (millis() - pump_start_time_ < PUMP_STABILIZE_TIME) {
            return;
        }
    }

    // pH reading
    float ph_voltage = get_filtered_reading_(ph_address_, ph_gain_);
    if (!std::isnan(ph_voltage)) {
        float ph_value = convert_to_ph(ph_voltage);
        // Reading valid

        if (!std::isnan(ph_value) && temp_sensor_ != nullptr && !std::isnan(temp_sensor_->state)) {
            float temp_c = temp_sensor_->state;
            float raw_ph = ph_value;
            ph_value = compensate_ph_for_temperature_(ph_value, temp_c);
            ESP_LOGD(TAG, "pH: %.2f (raw) -> %.2f (temp compensated at %.1f°C)",
                     raw_ph, ph_value, temp_c);
        }

        if (!std::isnan(ph_value) && ph_sensor_ != nullptr) {
            ph_sensor_->publish_state(ph_value);
        }
        this->publish_state(ph_voltage);
        check_ph_limits_(ph_value);
    } else {
        // Reading invalid
        ESP_LOGW(TAG, "pH reading returned NAN");
    }

    // ORP reading
    float orp_voltage = get_filtered_reading_(orp_address_, orp_gain_);
    if (!std::isnan(orp_voltage)) {
        if (orp_voltage_sensor_ != nullptr) {
            orp_voltage_sensor_->publish_state(orp_voltage);
        }
        float orp_value = convert_to_orp(orp_voltage);
        if (!std::isnan(orp_value) && orp_sensor_ != nullptr) {
            orp_sensor_->publish_state(orp_value);
        }
        check_orp_limits_(orp_value);
    } else {
        ESP_LOGW(TAG, "ORP reading returned NAN");
    }

    update_calibration_age_();
}

// ============================================================
// convert_to_ph()
// ============================================================
float APAPHX_ADS1115::convert_to_ph(float voltage) {
    float mv = voltage * 1000.0f;

    if (abs(cal_data_.ph_ref2_mv - cal_data_.ph_ref1_mv) < 0.001f) {
        return NAN;
    }

    float ph = cal_data_.ph_ref1_value +
              (cal_data_.ph_ref2_value - cal_data_.ph_ref1_value) *
              (mv - cal_data_.ph_ref1_mv) /
              (cal_data_.ph_ref2_mv - cal_data_.ph_ref1_mv);

    return std::min(std::max(ph, 0.0f), 14.0f);
}

// ============================================================
// convert_to_orp()
// NOTE: clamp expanded to +/-2000mV in Step 3
// ============================================================
float APAPHX_ADS1115::convert_to_orp(float voltage) {
    float mv = voltage * 1000.0f;

    // Return NAN when uncalibrated -- consistent with convert_to_ph()
    // Returning raw mV as ORP mV would be misleading (different units/scale)
    if (abs(cal_data_.orp_ref2_mv - cal_data_.orp_ref1_mv) < 0.001f) {
        return NAN;
    }

    float orp = cal_data_.orp_ref1_value +
                (cal_data_.orp_ref2_value - cal_data_.orp_ref1_value) *
                (mv - cal_data_.orp_ref1_mv) /
                (cal_data_.orp_ref2_mv - cal_data_.orp_ref1_mv);

    return std::min(std::max(orp, -2000.0f), 2000.0f);
}

// ============================================================
// Calibration persistence
// ============================================================
void APAPHX_ADS1115::load_calibration_() {
    if (!this->pref_.load(&this->cal_data_)) {
        cal_data_ = {0, 0, 4.0f, 7.0f, 0, 0, 0, 475.0f, 650.0f, 0};
        ESP_LOGD(TAG, "No calibration found -- using defaults");
    } else {
        ESP_LOGD(TAG, "Calibration loaded from storage");
    }
    calibration_valid_ = (cal_data_.ph_last_calibration > 0 ||
                        cal_data_.orp_last_calibration > 0);
}

void APAPHX_ADS1115::save_calibration_() {
    this->pref_.save(&this->cal_data_);
}

void APAPHX_ADS1115::update_calibration_age_() {
    time_t now = ::time(nullptr);

    // Guard against publishing garbage before NTP sync
    // Unix timestamp < 1000000000 means time is not yet valid (pre year 2001)
    if (now < 1000000000UL) return;

    if (this->ph_cal_age_sensor_ != nullptr && cal_data_.ph_last_calibration > 0) {
        float ph_age_days = (now - cal_data_.ph_last_calibration) / 86400.0f;
        this->ph_cal_age_sensor_->publish_state(ph_age_days);
    }

    if (this->orp_cal_age_sensor_ != nullptr && cal_data_.orp_last_calibration > 0) {
        float orp_age_days = (now - cal_data_.orp_last_calibration) / 86400.0f;
        this->orp_cal_age_sensor_->publish_state(orp_age_days);
    }
}

// ============================================================
// Calibration actions
// ============================================================
void APAPHX_ADS1115::calibrate_ph_point1(float ph_value) {
    float voltage = get_stable_reading(ph_address_, ph_gain_);
    if (!std::isnan(voltage)) {
        cal_data_.ph_ref1_mv = voltage * 1000.0f;
        cal_data_.ph_ref1_value = ph_value;
        cal_data_.ph_last_calibration = ::time(nullptr);
        save_calibration_();
        update_calibration_age_();
        calibration_valid_ = true;
        // Clear rolling average -- ring contains pre-calibration values
        ph_avg_filled_ = 0; ph_avg_index_ = 0;
        // Note: calibrated sensor only goes ON after point 2 (two-point cal complete)
        ESP_LOGI(TAG, "pH Point 1 calibrated: %.2f pH at %.3f mV -- now calibrate point 2", ph_value, voltage * 1000.0f);
    } else {
        ESP_LOGE(TAG, "pH Point 1 calibration failed: could not get stable reading");
    }
}

void APAPHX_ADS1115::calibrate_ph_point2(float ph_value) {
    float voltage = get_stable_reading(ph_address_, ph_gain_);
    if (!std::isnan(voltage)) {
        cal_data_.ph_ref2_mv = voltage * 1000.0f;
        cal_data_.ph_ref2_value = ph_value;
        cal_data_.ph_last_calibration = ::time(nullptr);
        save_calibration_();
        update_calibration_age_();
        calibration_valid_ = true;
        // Clear rolling average -- ring contains pre-calibration values
        ph_avg_filled_ = 0; ph_avg_index_ = 0;
        if (ph_calibrated_sensor_ != nullptr) ph_calibrated_sensor_->publish_state(true);
        ESP_LOGI(TAG, "pH Point 2 calibrated: %.2f pH at %.3f mV", ph_value, voltage * 1000.0f);

        // Slope validation -- Nernst equation at 25C = 59.16 mV/pH
        // Acceptable electrode range: 40-70 mV/pH (67-118% of theoretical)
        float mv_span  = cal_data_.ph_ref2_mv   - cal_data_.ph_ref1_mv;
        float ph_span  = cal_data_.ph_ref2_value - cal_data_.ph_ref1_value;
        if (ph_span != 0.0f) {
            float slope = mv_span / ph_span;
            if (slope < 0.0f) slope = -slope;  // sign depends on hardware polarity
            float slope_pct = (slope / 59.16f) * 100.0f;
            if (slope < 40.0f || slope > 70.0f) {
                ESP_LOGW(TAG, "pH electrode slope out of range: %.1f mV/pH (%.0f%% of Nernst) -- check probe or buffers",
                         slope, slope_pct);
            } else if (slope_pct >= 95.0f) {
                ESP_LOGI(TAG, "pH electrode slope: %.1f mV/pH (%.0f%% Nernst -- excellent)",
                         slope, slope_pct);
            } else {
                ESP_LOGI(TAG, "pH electrode slope: %.1f mV/pH (%.0f%% Nernst -- acceptable)",
                         slope, slope_pct);
            }
        }
    } else {
        ESP_LOGE(TAG, "pH Point 2 calibration failed: could not get stable reading");
    }
}

void APAPHX_ADS1115::calibrate_orp_point1(float mv_value) {
    float voltage = get_stable_reading(orp_address_, orp_gain_);
    if (!std::isnan(voltage)) {
        cal_data_.orp_ref1_mv = voltage * 1000.0f;
        cal_data_.orp_ref1_value = mv_value;
        cal_data_.orp_last_calibration = ::time(nullptr);
        save_calibration_();
        update_calibration_age_();
        calibration_valid_ = true;
        // Clear rolling average -- ring contains pre-calibration values
        orp_avg_filled_ = 0; orp_avg_index_ = 0;
        ESP_LOGI(TAG, "ORP Point 1 calibrated: %.1f mV at %.3f mV raw", mv_value, voltage * 1000.0f);
    } else {
        ESP_LOGE(TAG, "ORP Point 1 calibration failed: could not get stable reading");
    }
}

void APAPHX_ADS1115::calibrate_orp_point2(float mv_value) {
    float voltage = get_stable_reading(orp_address_, orp_gain_);
    if (!std::isnan(voltage)) {
        cal_data_.orp_ref2_mv = voltage * 1000.0f;
        cal_data_.orp_ref2_value = mv_value;
        cal_data_.orp_last_calibration = ::time(nullptr);
        save_calibration_();
        update_calibration_age_();
        calibration_valid_ = true;
        // Clear rolling average -- ring contains pre-calibration values
        orp_avg_filled_ = 0; orp_avg_index_ = 0;
        if (orp_calibrated_sensor_ != nullptr) orp_calibrated_sensor_->publish_state(true);
        ESP_LOGI(TAG, "ORP Point 2 calibrated: %.1f mV at %.3f mV raw", mv_value, voltage * 1000.0f);

        // Slope validation for ORP -- check span between calibration points
        // A healthy ORP probe should show clear separation between reference solutions
        // Minimum acceptable span: 50mV between the two reference points
        float mv_span = cal_data_.orp_ref2_value - cal_data_.orp_ref1_value;
        float raw_span = cal_data_.orp_ref2_mv - cal_data_.orp_ref1_mv;
        if (mv_span < 0.0f) mv_span = -mv_span;
        if (raw_span < 0.0f) raw_span = -raw_span;
        if (raw_span < 50.0f) {
            ESP_LOGW(TAG, "ORP calibration span too small: %.1f mV raw (expected >50mV) -- check probe or solutions",
                     raw_span);
        } else {
            ESP_LOGI(TAG, "ORP calibration span: %.1f mV raw across %.1f mV reference -- good",
                     raw_span, mv_span);
        }
    } else {
        ESP_LOGE(TAG, "ORP Point 2 calibration failed: could not get stable reading");
    }
}

void APAPHX_ADS1115::reset_calibration() {
    cal_data_ = {0, 0, 4.0f, 7.0f, 0, 0, 0, 475.0f, 650.0f, 0};
    save_calibration_();
    update_calibration_age_();
    calibration_valid_ = false;
    if (ph_calibrated_sensor_  != nullptr) ph_calibrated_sensor_->publish_state(false);
    if (orp_calibrated_sensor_ != nullptr) orp_calibrated_sensor_->publish_state(false);
    ESP_LOGI(TAG, "Calibration reset to defaults");
}

}  // namespace apaphx_ads1115
}  // namespace esphome
