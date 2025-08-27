#include "switch.h"
#include "esphome/core/log.h"

namespace esphome {
namespace luxpower_sna {

static const char *const SWITCH_TAG = "luxpower_sna.switch";  // Changed from TAG to avoid conflict

void LuxPowerSwitch::setup() {
  if (parent_ == nullptr) {
    ESP_LOGE(SWITCH_TAG, "Parent component not set!");
    mark_failed();
    return;
  }
  
  ESP_LOGCONFIG(SWITCH_TAG, "Setting up LuxPower Switch: %s", switch_type_.c_str());
  
  // Try to get initial state
  if (parent_->is_connection_ready()) {
    parent_->read_register_async(register_address_, [this](uint16_t value) {
      update_state_from_register_(value);
    });
  }
}

void LuxPowerSwitch::loop() {
  // Periodically sync state with inverter
  uint32_t now = millis();
  if (now - last_read_attempt_ > read_interval_ && parent_->is_connection_ready()) {
    last_read_attempt_ = now;
    
    parent_->read_register_async(register_address_, [this](uint16_t value) {
      update_state_from_register_(value);
    });
  }
}

void LuxPowerSwitch::dump_config() {
  ESP_LOGCONFIG(SWITCH_TAG, "LuxPower Switch '%s'", switch_type_.c_str());
  ESP_LOGCONFIG(SWITCH_TAG, "  Register: %d (0x%02X)", register_address_, register_address_);
  ESP_LOGCONFIG(SWITCH_TAG, "  Bitmask: 0x%04X", bitmask_);
  LOG_SWITCH("", "LuxPower Switch", this);
}

void LuxPowerSwitch::write_state(bool state) {
  if (parent_ == nullptr || !parent_->is_connection_ready()) {
    ESP_LOGE(SWITCH_TAG, "Parent component not available or not connected");
    return;
  }

  ESP_LOGI(SWITCH_TAG, "Setting '%s' to %s", switch_type_.c_str(), state ? "ON" : "OFF");

  // Read current register value first
  parent_->read_register_async(register_address_, [this, state](uint16_t current_value) {
    // Update our cache
    this->cached_register_value_ = current_value;
    this->has_cached_value_ = true;
    
    // Calculate new value with bit manipulation
    uint16_t new_value = this->prepare_binary_value_(current_value, this->bitmask_, state);
    
    ESP_LOGD(SWITCH_TAG, "Writing register %d for '%s': 0x%04X -> 0x%04X", 
             this->register_address_, this->switch_type_.c_str(), current_value, new_value);
    
    // Write the new value - capture 'this' to access member variables
    this->parent_->write_register_async(this->register_address_, new_value, [this, state, new_value](bool success) {
      if (success) {
        ESP_LOGI(SWITCH_TAG, "Successfully set '%s' to %s", this->switch_type_.c_str(), state ? "ON" : "OFF");
        this->cached_register_value_ = new_value;  // Update cache
        this->publish_state(state);
      } else {
        ESP_LOGE(SWITCH_TAG, "Failed to write register for '%s'", this->switch_type_.c_str());
      }
    });
  });
}

uint16_t LuxPowerSwitch::prepare_binary_value_(uint16_t old_value, uint16_t mask, bool enable) {
  // Direct port from Python LXPPacket.prepare_binary_value()
  return enable ? (old_value | mask) : (old_value & (65535 - mask));
}

void LuxPowerSwitch::update_state_from_register_(uint16_t reg_value) {
  cached_register_value_ = reg_value;
  has_cached_value_ = true;
  
  bool current_state = (reg_value & bitmask_) == bitmask_;
  
  // Only publish if state actually changed
  if (current_state != state) {
    ESP_LOGD(SWITCH_TAG, "State sync for '%s': %s (register: 0x%04X, mask: 0x%04X)", 
             switch_type_.c_str(), current_state ? "ON" : "OFF", reg_value, bitmask_);
    publish_state(current_state);
  }
}

}  // namespace luxpower_sna
}  // namespace esphome
