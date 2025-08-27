#include "esphome/core/log.h"
#include "../luxpower_sna.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna.switch";

void LuxPowerSwitch::setup() {
  if (parent_ == nullptr) {
    ESP_LOGE(TAG, "Parent component not set!");
    mark_failed();
    return;
  }
  
  ESP_LOGCONFIG(TAG, "Setting up LuxPower Switch: %s", switch_type_.c_str());
  
  // Register this switch with the parent for state updates
  parent_->register_switch(register_address_, this);
  
  // Get initial state if register is cached
  if (parent_->has_cached_register(register_address_)) {
    uint16_t reg_value = parent_->get_cached_register(register_address_);
    bool initial_state = (reg_value & bitmask_) == bitmask_;
    ESP_LOGD(TAG, "Initial state for %s: %s (reg: 0x%04X, mask: 0x%04X)", 
             switch_type_.c_str(), initial_state ? "ON" : "OFF", reg_value, bitmask_);
    publish_state(initial_state);
  }
}

void LuxPowerSwitch::dump_config() {
  ESP_LOGCONFIG(TAG, "LuxPower Switch '%s'", switch_type_.c_str());
  ESP_LOGCONFIG(TAG, "  Register: %d (0x%02X)", register_address_, register_address_);
  ESP_LOGCONFIG(TAG, "  Bitmask: 0x%04X", bitmask_);
  LOG_SWITCH("", "LuxPower Switch", this);
}

void LuxPowerSwitch::write_state(bool state) {
  if (parent_ == nullptr) {
    ESP_LOGE(TAG, "Parent component not available");
    return;
  }

  ESP_LOGI(TAG, "Setting '%s' to %s (register %d, mask 0x%04X)", 
           switch_type_.c_str(), state ? "ON" : "OFF", register_address_, bitmask_);

  // Delegate everything to the parent component
  parent_->read_register(register_address_, [this, state](uint16_t current_value) {
    if (current_value == 0 && !parent_->has_cached_register(register_address_)) {
      ESP_LOGE(TAG, "Failed to read current register value for %s", switch_type_.c_str());
      return;
    }
    
    // Use parent's bit manipulation method (from prepare_binary_value)
    uint16_t new_value = parent_->prepare_binary_value(current_value, bitmask_, state);
    
    ESP_LOGD(TAG, "Writing register %d for '%s': 0x%04X -> 0x%04X", 
             register_address_, switch_type_.c_str(), current_value, new_value);
    
    // Write back the modified value through parent
    parent_->write_register(register_address_, new_value, [this, state](bool success) {
      if (success) {
        ESP_LOGI(TAG, "Successfully set '%s' to %s", switch_type_.c_str(), state ? "ON" : "OFF");
        publish_state(state);
      } else {
        ESP_LOGE(TAG, "Failed to write register for '%s'", switch_type_.c_str());
        // Don't publish state on failure - keep current state
      }
    });
  });
}

}  // namespace luxpower_sna
}  // namespace esphome
