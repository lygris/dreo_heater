#pragma once
#include "esphome/components/tuya/tuya.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/number/number.h"
#include <cmath>
#include <deque>
#include <vector>
#include <initializer_list>
#include <algorithm>
#include <cstring>
#include <numeric>
#include <iterator>

namespace esphome {
namespace dreo_heater {

// Custom Presets
const char *PRESET_H1 = "H1";
const char *PRESET_H2 = "H2";
const char *PRESET_H3 = "H3";

// DP IDs (Tuya Data Points)
enum TuyaDP {
    DP_POWER = 1,
    DP_MODE = 2,
    DP_HEAT_LEVEL = 3,
    DP_TARGET_TEMP = 4,
    DP_CURRENT_TEMP = 7,
    DP_SOUND = 6,
    DP_DISPLAY = 8,
    DP_TIMER = 9,
    DP_CALIBRATION = 15,
    DP_CHILD_LOCK = 16,
    DP_HEATING_STATUS = 19,
    DP_WINDOW_DETECTION = 20,
    DP_TEMP_UNIT = 22
};

class DreoHeater : public climate::Climate, public tuya::Tuya, public Component {
 public:
  DreoHeater(uart::UARTComponent *parent) : tuya::Tuya(parent) {}

  bool debug_mode{false};
  void set_debug(bool enable) { this->debug_mode = enable; }

  switch_::Switch *sound_switch{nullptr};
  switch_::Switch *display_switch{nullptr};
  switch_::Switch *child_lock_switch{nullptr};
  switch_::Switch *window_switch{nullptr};
  switch_::Switch *temp_unit_switch{nullptr};
  switch_::Switch *unit_switch{nullptr};

  number::Number *heat_level_number{nullptr};
  number::Number *timer_number{nullptr};
  number::Number *calibration_number{nullptr};

  void set_temp_unit_switch(switch_::Switch *s) { temp_unit_switch = s; unit_switch = s; }
  void set_unit_switch(switch_::Switch *s) { unit_switch = s; temp_unit_switch = s; }

  // Commands
  void set_temp_unit(bool is_celsius) {
    send_tuya_dp(0x16, 0x04, 1, {(uint8_t)(is_celsius ? 2 : 1)});
  }
  void set_power(bool power) { send_tuya_dp(DP_POWER, 0x01, 1, {(uint8_t)(power ? 1 : 0)}); }
  void set_mode(int mode) { send_tuya_dp(DP_MODE, 0x04, 1, {(uint8_t)mode}); }
  void set_heat_level(int level) { send_tuya_dp(DP_HEAT_LEVEL, 0x02, 1, {(uint8_t)level}); }
  void set_temperature(int temp_f) { send_tuya_dp(DP_TARGET_TEMP, 0x02, 1, {(uint8_t)temp_f}); }
  void set_sound(bool on) { send_tuya_dp(DP_SOUND, 0x01, 1, {(uint8_t)(on ? 1 : 0)}); }
  void set_display(bool on) { send_tuya_dp(DP_DISPLAY, 0x01, 1, {(uint8_t)(on ? 1 : 0)}); }
  void set_child_lock(bool on) { send_tuya_dp(DP_CHILD_LOCK, 0x01, 1, {(uint8_t)(on ? 1 : 0)}); }
  void set_window_mode(bool on) { send_tuya_dp(DP_WINDOW_DETECTION, 0x01, 1, {(uint8_t)(on ? 1 : 0)}); }
  void set_timer(int min) { send_tuya_dp(DP_TIMER, 0x02, 2, {(uint8_t)(min >> 8), (uint8_t)(min & 0xFF)}); }
  void set_calibration(int cal) { send_tuya_dp(DP_CALIBRATION, 0x02, 1, {(uint8_t)cal}); }

  void setup() override {
      ESP_LOGI("dreo", "Dreo Climate Initialized");
      send_tuya_raw(0x00, {}); delay(50);
      send_tuya_raw(0x03, {0x02, 0x05, 0x00}); delay(50);
      send_tuya_raw(0x02, {}); 
  }

  climate::ClimateTraits traits() override {
    auto traits = climate::ClimateTraits();
    traits.set_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_ACTION);
    traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_FAN_ONLY});
    traits.set_supported_presets({climate::CLIMATE_PRESET_NONE, climate::CLIMATE_PRESET_ECO});
    traits.set_supported_custom_presets({PRESET_H1, PRESET_H2, PRESET_H3});
    traits.set_visual_min_temperature(5.0);
    traits.set_visual_max_temperature(35.0);
    traits.set_visual_temperature_step(1.0); 
    return traits;
  }

  void control(const climate::ClimateCall &call) override {
    // Handle presets
    if (!call.get_custom_preset().empty()) {
        auto preset_ref = call.get_custom_preset(); 
        int level = 0;
        if (preset_ref == PRESET_H1) level = 1;
        else if (preset_ref == PRESET_H2) level = 2;
        else if (preset_ref == PRESET_H3) level = 3;

        if (level > 0) {
            set_power(true); delay(50);
            set_mode(1); delay(50);
            set_heat_level(level);
            this->mode = climate::CLIMATE_MODE_HEAT;
            this->set_custom_preset_(preset_ref.c_str());
            this->preset = climate::CLIMATE_PRESET_NONE;
            this->publish_state(); return; 
        }
    }

    if (call.get_preset().has_value()) {
        auto p = *call.get_preset();
        if (p == climate::CLIMATE_PRESET_ECO) {
            set_power(true); delay(50); set_mode(2);
            this->mode = climate::CLIMATE_MODE_HEAT;
            this->preset = climate::CLIMATE_PRESET_ECO;
            this->set_custom_preset_("");
            this->publish_state(); return;
        } else if (p == climate::CLIMATE_PRESET_NONE) {
            this->preset = climate::CLIMATE_PRESET_NONE;
            this->set_custom_preset_("");
            this->publish_state();
        }
    }

    if (call.get_mode().has_value()) {
      climate::ClimateMode mode = *call.get_mode();
      if (mode == climate::CLIMATE_MODE_OFF) {
        set_power(false);
        this->mode = climate::CLIMATE_MODE_OFF;
        this->action = climate::CLIMATE_ACTION_OFF;
        this->preset = climate::CLIMATE_PRESET_NONE;
        this->set_custom_preset_("");
      } else {
        set_power(true); delay(100); 
        if (mode == climate::CLIMATE_MODE_HEAT) {
            set_mode(2); this->preset = climate::CLIMATE_PRESET_ECO; this->set_custom_preset_("");
        } else if (mode == climate::CLIMATE_MODE_FAN_ONLY) {
            set_mode(3); this->preset = climate::CLIMATE_PRESET_NONE; this->set_custom_preset_("");
        }
        this->mode = mode;
      }
      this->publish_state();
    }

    if (call.get_target_temperature().has_value()) {
      float temp_c = *call.get_target_temperature();
      uint8_t temp_f = (uint8_t)std::clamp((int)(temp_c * 1.8f + 32.0f + 0.5f), 41, 95);
      set_temperature(temp_f);
      this->target_temperature = temp_c;
      this->publish_state();
    }
  }

  void loop() override {
    uint32_t now = millis();
    if (now - last_hb_ > 10000) {
        send_tuya_raw(0x00, {});
        last_hb_ = now;
    }
    while (available()) {
      uint8_t b = read();
      rx_buf_[tail_] = b;
      tail_ = (tail_ + 1) % RX_BUF_SIZE;
      if (tail_ == head_) head_ = (head_ + 1) % RX_BUF_SIZE;
    }
    if (head_ != tail_) parse_rx();
  }

  void parse_rx() {
      while (get_buf_size() >= 9) { 
          if (rx_buf_[head_] != 0x55) { head_ = (head_ + 1) % RX_BUF_SIZE; continue; }
          if (rx_buf_[(head_ + 1) % RX_BUF_SIZE] != 0xAA) { head_ = (head_ + 1) % RX_BUF_SIZE; continue; }

          uint16_t payload_len = (rx_buf_[(head_ + 6) % RX_BUF_SIZE] << 8) | rx_buf_[(head_ + 7) % RX_BUF_SIZE];
          uint16_t packet_len = 8 + payload_len + 1; 
          if (get_buf_size() < packet_len) return;
          
          uint8_t received_sum = rx_buf_[(head_ + packet_len - 1) % RX_BUF_SIZE];
          uint32_t calc_sum = 0;
          for (int i = 2; i < packet_len - 1; i++) calc_sum += rx_buf_[(head_ + i) % RX_BUF_SIZE];
          
          if (received_sum == (uint8_t)((calc_sum - 1) & 0xFF)) {
              if (rx_buf_[(head_ + 4) % RX_BUF_SIZE] == 0x07 || rx_buf_[(head_ + 4) % RX_BUF_SIZE] == 0x08) {
                  process_status(8, payload_len);
              }
              head_ = (head_ + packet_len) % RX_BUF_SIZE;
          } else head_ = (head_ + 1) % RX_BUF_SIZE;
      }
  }
  
  uint16_t get_buf_size() { return (tail_ >= head_) ? (tail_ - head_) : (RX_BUF_SIZE - head_ + tail_); }

  void process_status(int start_offset, int len) {
      bool changed = false;
      int idx = (head_ + start_offset) % RX_BUF_SIZE;
      int end_idx = (head_ + start_offset + len) % RX_BUF_SIZE;

      while (true) {
          uint8_t dp_id = rx_buf_[idx];
          uint8_t dp_len = rx_buf_[(idx + 4) % RX_BUF_SIZE];
          uint32_t val = 0;
          for(int i=0; i<dp_len; i++) val = (val << 8) + rx_buf_[(idx + 5 + i) % RX_BUF_SIZE];
          
          if (this->debug_mode) ESP_LOGD("dreo", "DP ID: %d Val: %d", dp_id, val);

          switch (dp_id) {
              case DP_POWER: handle_power(val); changed = true; break;
              case DP_MODE: handle_mode(val); changed = true; break;
              case DP_HEAT_LEVEL: handle_heat_level(val); changed = true; break;
              case DP_TARGET_TEMP: this->target_temperature = ((float)val - 32.0f) * 5.0f / 9.0f; changed = true; break;
              case DP_CURRENT_TEMP: this->current_temperature = ((float)val - 32.0f) * 5.0f / 9.0f; changed = true; break;
              case DP_HEATING_STATUS: handle_heating_status(val); changed = true; break;
              case DP_SOUND: if (sound_switch) sound_switch->publish_state(val == 0); break;
              case DP_DISPLAY: if (display_switch) display_switch->publish_state(val != 0); break;
              case DP_TIMER: if (timer_number) timer_number->publish_state(val); break;
              case DP_CALIBRATION: if (calibration_number) calibration_number->publish_state((int)val); break;
              case DP_CHILD_LOCK: if (child_lock_switch) child_lock_switch->publish_state(val != 0); break;
              case DP_WINDOW_DETECTION: if (window_switch) window_switch->publish_state(val != 0); break;
              case DP_TEMP_UNIT: if (unit_switch) unit_switch->publish_state(val == 2); break;
          }
          idx = (idx + 5 + dp_len) % RX_BUF_SIZE;
          if (idx == end_idx) break;
      }
      if (changed) this->publish_state();
  }

  void handle_power(uint32_t val) {
      if (val == 0) { this->mode = climate::CLIMATE_MODE_OFF; this->action = climate::CLIMATE_ACTION_OFF; this->set_custom_preset_(""); this->preset = climate::CLIMATE_PRESET_NONE; }
      else if (this->mode == climate::CLIMATE_MODE_OFF) { this->mode = climate::CLIMATE_MODE_HEAT; this->preset = climate::CLIMATE_PRESET_ECO; }
  }

  void handle_mode(uint32_t val) {
      if (this->mode == climate::CLIMATE_MODE_OFF) return;
      if (val == 2) { this->mode = climate::CLIMATE_MODE_HEAT; this->preset = climate::CLIMATE_PRESET_ECO; this->set_custom_preset_(""); }
      else if (val == 3) { this->mode = climate::CLIMATE_MODE_FAN_ONLY; this->preset = climate::CLIMATE_PRESET_NONE; this->set_custom_preset_(""); }
      else if (val == 1) { this->mode = climate::CLIMATE_MODE_HEAT; this->preset = climate::CLIMATE_PRESET_NONE; }
  }

  void handle_heat_level(uint32_t val) {
      if (heat_level_number) heat_level_number->publish_state(val);
      if (this->mode == climate::CLIMATE_MODE_HEAT && this->preset != climate::CLIMATE_PRESET_ECO) {
          if (val == 1) this->set_custom_preset_(PRESET_H1);
          else if (val == 2) this->set_custom_preset_(PRESET_H2);
          else if (val == 3) this->set_custom_preset_(PRESET_H3);
      }
  }

  void handle_heating_status(uint32_t val) {
      if (this->mode == climate::CLIMATE_MODE_OFF) return;
      if (val == 1) this->action = climate::CLIMATE_ACTION_HEATING;
      else this->action = (this->mode == climate::CLIMATE_MODE_FAN_ONLY) ? climate::CLIMATE_ACTION_FAN : climate::CLIMATE_ACTION_IDLE;
  }

 protected:
  static const size_t RX_BUF_SIZE = 512;
  uint8_t rx_buf_[RX_BUF_SIZE];
  uint16_t head_{0}, tail_{0};
  uint32_t last_hb_{0};
};

} // namespace dreo_heater
} // namespace esphome
