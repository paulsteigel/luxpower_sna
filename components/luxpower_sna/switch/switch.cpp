#include "switch.h"
#include "esphome/core/log.h"

namespace esphome {
namespace luxpower_sna {

static const char *const SWITCH_TAG = "luxpower_sna.switch";

void LuxPowerSwitch::setup() {
  ESP_LOGCONFIG(SWITCH_TAG, "Setting up LuxPower Switch: %s", this->get_name().c_str());
  
  if (parent_ == nullptr) {
    ESP_LOGE(SWITCH_TAG, "Parent component not set!");
    mark_failed();
    return;
  }
  
  if (register_address_ == 0) {
    ESP_LOGE(SWITCH_TAG, "Register address not set!");
    mark_failed();
    return;
  }
  
  if (bitmask_ == 0) {
    ESP_LOGE(SWITCH_TAG, "Bitmask not set!");
    mark_failed();
    return;
  }
  
  // Schedule initial state read after parent has time to initialize
  this->set_timeout(5000, [this]() {
    this->read_current_state_();
  });
}

void LuxPowerSwitch::dump_config() {
  ESP_LOGCONFIG(SWITCH_TAG, "LuxPower Switch '%s':", this->get_name().c_str());
  ESP_LOGCONFIG(SWITCH_TAG, "  Register: %d (0x%04X)", register_address_, register_address_);
  ESP_LOGCONFIG(SWITCH_TAG, "  Bitmask: 0x%04X", bitmask_);
  
  if (parent_ == nullptr) {
    ESP_LOGCONFIG(SWITCH_TAG, "  Status: FAILED - No parent component");
  } else {
    ESP_LOGCONFIG(SWITCH_TAG, "  Status: OK");
  }
  
  LOG_SWITCH("", "LuxPower Switch", this);
}

void LuxPowerSwitch::write_state(bool state) {
  ESP_LOGD(SWITCH_TAG, "write_state called for '%s' with state: %s", 
           this->get_name().c_str(), state ? "ON" : "OFF");
  
  if (parent_ == nullptr) {
    ESP_LOGE(SWITCH_TAG, "Parent component is null!");
    return;
  }
  
  // Check if parent is ready for operations
  if (!parent_->is_connection_ready()) {
    ESP_LOGW(SWITCH_TAG, "Parent not ready for switch operation, deferring...");
    // Defer the operation
    this->set_timeout(1000, [this, state]() {
      this->write_state(state);
    });
    return;
  }
  
  // Prevent too frequent writes
  uint32_t now = millis();
  if (pending_write_ && (now - last_write_time_ < 2000)) {
    ESP_LOGW(SWITCH_TAG, "Write request too soon after previous write for '%s', ignoring", 
             this->get_name().c_str());
    // Revert UI state
    this->publish_state(!state);
    return;
  }
  
  ESP_LOGI(SWITCH_TAG, "Setting '%s' to %s", this->get_name().c_str(), state ? "ON" : "OFF");
  
  pending_write_ = true;
  last_write_time_ = now;

  // First, read the current register value
  parent_->read_register_async(register_address_, [this, state](uint16_t current_value) {
    ESP_LOGD(SWITCH_TAG, "Current register %d value: 0x%04X", this->register_address_, current_value);
    
    // Calculate new value with bit manipulation
    uint16_t new_value = this->prepare_binary_value_(current_value, this->bitmask_, state);
    
    ESP_LOGD(SWITCH_TAG, "Writing register %d for '%s': 0x%04X -> 0x%04X (bitmask: 0x%04X)", 
             this->register_address_, this->get_name().c_str(),
             current_value, new_value, this->bitmask_);
    
    // Only write if the value actually needs to change
    if (new_value != current_value) {
      // Write the new value
      this->parent_->write_register_async(this->register_address_, new_value, [this, state](bool success) {
        this->pending_write_ = false;
        
        if (success) {
          ESP_LOGI(SWITCH_TAG, "Successfully set '%s' to %s", 
                   this->get_name().c_str(), state ? "ON" : "OFF");
          this->publish_state(state);
          
          // Schedule a verification read after a short delay
          this->set_timeout(2000, [this]() {
            this->read_current_state_();
          });
          
        } else {
          ESP_LOGE(SWITCH_TAG, "Failed to write register for '%s'", this->get_name().c_str());
          
          // Read current state to sync with actual hardware state
          this->set_timeout(1000, [this]() {
            this->read_current_state_();
          });
        }
      });
    } else {
      // Value is already correct, just update UI
      ESP_LOGD(SWITCH_TAG, "Register %d already has correct value for '%s'", 
               this->register_address_, this->get_name().c_str());
      this->pending_write_ = false;
      this->publish_state(state);
    }
  });
}

void LuxPowerSwitch::read_current_state_() {
  if (parent_ == nullptr) {
    ESP_LOGW(SWITCH_TAG, "Cannot read state - parent component not available");
    return;
  }
  
  ESP_LOGV(SWITCH_TAG, "Reading current state for '%s' from register %d", 
           this->get_name().c_str(), register_address_);
  
  parent_->read_register_async(register_address_, [this](uint16_t value) {
    ESP_LOGD(SWITCH_TAG, "Read register %d for '%s': 0x%04X", 
             this->register_address_, this->get_name().c_str(), value);
    this->update_state_from_register_(value);
  });
}

uint16_t LuxPowerSwitch::prepare_binary_value_(uint16_t old_value, uint16_t mask, bool enable) {
  if (enable) {
    // Set the bits specified by mask
    return old_value | mask;
  } else {
    // Clear the bits specified by mask
    return old_value & (~mask);
  }
}

void LuxPowerSwitch::update_state_from_register_(uint16_t reg_value) {
  // Check if ALL bits in the mask are set
  bool current_state = (reg_value & bitmask_) == bitmask_;
  
  // Only publish if this is the initial read or if the state actually changed
  if (!initial_state_read_ || current_state != state) {
    ESP_LOGD(SWITCH_TAG, "State update for '%s': %s (register: 0x%04X, mask: 0x%04X, masked_value: 0x%04X)", 
             this->get_name().c_str(), current_state ? "ON" : "OFF", 
             reg_value, bitmask_, reg_value & bitmask_);
    publish_state(current_state);
  }
  
  initial_state_read_ = true;
}

}  // namespace luxpower_sna
}  // namespace esphome
