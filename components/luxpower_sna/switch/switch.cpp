#include "switch.h"
#include "esphome/core/log.h"

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
  
  // Get initial state
  sync_state_();
}

void LuxPowerSwitch::loop() {
  uint32_t now = millis();
  if (now - last_sync_ > SYNC_INTERVAL) {
    last_sync_ = now;
    sync_state_();
  }
}

void LuxPowerSwitch::dump_config() {
  ESP_LOGCONFIG(TAG, "LuxPower Switch '%s'", switch_type_.c_str());
  ESP_LOGCONFIG(TAG, "  Register: %d (0x%02X)", register_address_, register_address_);
  ESP_LOGCONFIG(TAG, "  Bitmask: 0x%04X", bitmask_);
  LOG_SWITCH("", "LuxPower Switch", this);
}

void LuxPowerSwitch::write_state(bool state) {
  if (parent_ == nullptr || !parent_->is_connection_ready()) {
    ESP_LOGW(TAG, "Cannot write '%s' - parent not ready", switch_type_.c_str());
    return;
  }

  ESP_LOGI(TAG, "Setting '%s' to %s", switch_type_.c_str(), state ? "ON" : "OFF");

  // Read current register value
  parent_->read_register_async(register_address_, [this, state](uint16_t current_value) {
    // Calculate new value
    uint16_t new_value = prepare_binary_value_(current_value, bitmask_, state);
    
    ESP_LOGD(TAG, "Writing register %d for '%s': 0x%04X -> 0x%04X", 
             register_address_, switch_type_.c_str(), current_value, new_value);
    
    // Write new value
    parent_->write_register_async(register_address_, new_value, [this, state](bool success) {
      if (success) {
        ESP_LOGI(TAG, "Successfully set '%s' to %s", switch_type_.c_str(), state ? "ON" : "OFF");
        publish_state(state);
      } else {
        ESP_LOGE(TAG, "Failed to write register for '%s'", switch_type_.c_str());
      }
    });
  });
}

void LuxPowerSwitch::sync_state_() {
  if (parent_ == nullptr || !parent_->is_connection_ready()) {
    return;
  }
  
  parent_->read_register_async(register_address_, [this](uint16_t reg_value) {
    bool current_state = (reg_value & bitmask_) == bitmask_;
    
    if (current_state != state) {
      ESP_LOGD(TAG, "Syncing '%s' state: %s", switch_type_.c_str(), current_state ? "ON" : "OFF");
      publish_state(current_state);
    }
  });
}

uint16_t LuxPowerSwitch::prepare_binary_value_(uint16_t old_value, uint16_t mask, bool enable) {
  return enable ? (old_value | mask) : (old_value & (65535 - mask));
}

}  // namespace luxpower_sna
}  // namespace esphome
