#include "switch.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace luxpower_sna {

void LuxPowerSwitch::setup() {
  ESP_LOGCONFIG(SWITCH_TAG, "Setting up LuxPower Switch '%s'...", this->get_name().c_str());
  
  if (!parent_) {
    ESP_LOGE(SWITCH_TAG, "Parent component not set for switch '%s'", this->get_name().c_str());
    mark_failed();
    return;
  }
  
  if (register_address_ == 0) {
    ESP_LOGE(SWITCH_TAG, "Register address not set for switch '%s'", this->get_name().c_str());
    mark_failed();
    return;
  }
  
  if (bitmask_ == 0) {
    ESP_LOGE(SWITCH_TAG, "Bitmask not set for switch '%s'", this->get_name().c_str());
    mark_failed();
    return;
  }
  
  // No individual timer - will be updated by parent component according to YAML interval
  ESP_LOGD(SWITCH_TAG, "Switch '%s' registered for centralized updates (reg %d, mask 0x%04X)", 
           this->get_name().c_str(), register_address_, bitmask_);
  
  // Initial state will be read during first centralized update cycle
}

void LuxPowerSwitch::dump_config() {
  ESP_LOGCONFIG(SWITCH_TAG, "LuxPower Switch:");
  ESP_LOGCONFIG(SWITCH_TAG, "  Name: %s", this->get_name().c_str());
  ESP_LOGCONFIG(SWITCH_TAG, "  Register: %d (0x%04X)", register_address_, register_address_);
  ESP_LOGCONFIG(SWITCH_TAG, "  Bitmask: 0x%04X", bitmask_);
  ESP_LOGCONFIG(SWITCH_TAG, "  Parent: %s", parent_ ? "Connected" : "Missing");
}

void LuxPowerSwitch::write_state(bool state) {
  if (!parent_) {
    ESP_LOGW(SWITCH_TAG, "Cannot write state - parent component not available");
    return;
  }
  
  if (pending_write_) {
    ESP_LOGW(SWITCH_TAG, "Write already pending for '%s', ignoring new request", this->get_name().c_str());
    return;
  }
  
  // IMMEDIATELY update the UI to show the desired state (optimistic update)
  this->publish_state(state);
  ESP_LOGI(SWITCH_TAG, "UI updated for '%s': %s (optimistic)", 
           this->get_name().c_str(), state ? "ON" : "OFF");
  
  if (!parent_->is_connection_ready()) {
    ESP_LOGI(SWITCH_TAG, "Parent busy, queuing write for '%s': %s", 
             this->get_name().c_str(), state ? "ON" : "OFF");
    
    pending_write_ = true;
    last_write_time_ = millis();
    
    // Store the desired state for retry
    this->set_timeout("retry_write", 2000, [this, state]() {
      if (this->pending_write_) {
        ESP_LOGD(SWITCH_TAG, "Retrying queued write for '%s'", this->get_name().c_str());
        this->pending_write_ = false;
        this->write_state_internal_(state); // Use internal method to avoid double UI update
      }
    });
    return;
  }
  
  // Parent is ready, execute immediately
  this->write_state_internal_(state);
}

void LuxPowerSwitch::verify_write_result_() {
  if (!parent_ || !parent_->is_connection_ready()) {
    // Skip verification if parent is busy
    return;
  }
  
  ESP_LOGV(SWITCH_TAG, "Verifying write result for '%s'", this->get_name().c_str());
  
  parent_->read_register_async(register_address_, [this](uint16_t value) {
    bool actual_state = (value & bitmask_) != 0;
    bool ui_state = this->state;
    
    if (actual_state != ui_state) {
      ESP_LOGW(SWITCH_TAG, "State mismatch for '%s': UI=%s, Actual=%s - correcting UI", 
               this->get_name().c_str(), 
               ui_state ? "ON" : "OFF", 
               actual_state ? "ON" : "OFF");
      this->publish_state(actual_state);
    } else {
      ESP_LOGD(SWITCH_TAG, "State verified for '%s': %s", 
               this->get_name().c_str(), actual_state ? "ON" : "OFF");
    }
  });
}

// New internal method that doesn't update UI (already updated above)
void LuxPowerSwitch::write_state_internal_(bool state) {
  ESP_LOGI(SWITCH_TAG, "Executing write for '%s': %s (register %d, mask 0x%04X)", 
           this->get_name().c_str(), state ? "ON" : "OFF", register_address_, bitmask_);
  
  pending_write_ = true;
  last_write_time_ = millis();
  
  // First read the current register value
  parent_->read_register_async(register_address_, [this, state](uint16_t current_value) {
    // Prepare the new value with the bit set/cleared
    uint16_t new_value = prepare_binary_value_(current_value, bitmask_, state);
    
    ESP_LOGD(SWITCH_TAG, "Switch '%s': current=0x%04X, new=0x%04X, mask=0x%04X", 
             this->get_name().c_str(), current_value, new_value, bitmask_);
    
    // Write the modified value back
    parent_->write_register_async(register_address_, new_value, [this, state](bool success) {
      this->handle_write_result_(success);
      
      if (success) {
        ESP_LOGI(SWITCH_TAG, "Successfully wrote state for '%s'", this->get_name().c_str());
        // Verify the write after a delay, but don't let it override UI immediately
        this->set_timeout("verify_write", 3000, [this]() {
          this->verify_write_result_();
        });
      } else {
        ESP_LOGW(SWITCH_TAG, "Failed to write state for '%s', reverting UI", this->get_name().c_str());
        // Only revert UI on actual failure
        this->set_timeout("revert_ui", 1000, [this]() {
          this->read_current_state_();
        });
      }
    });
  });
}

void LuxPowerSwitch::update_state_from_parent() {
  // Don't update UI if we have a recent write operation
  if (pending_write_) {
    uint32_t time_since_write = millis() - last_write_time_;
    if (time_since_write < 8000) {  // Give 8 seconds for write to complete and verify
      ESP_LOGV(SWITCH_TAG, "Skipping centralized update for '%s' - recent write operation", 
               this->get_name().c_str());
      return;
    }
  }
  
  if (!initial_state_read_) {
    ESP_LOGD(SWITCH_TAG, "Reading initial state for '%s'", this->get_name().c_str());
  } else {
    ESP_LOGV(SWITCH_TAG, "Centralized state update for '%s'", this->get_name().c_str());
  }
  
  this->read_current_state_();
}


void LuxPowerSwitch::read_current_state_() {
  if (!parent_) {
    ESP_LOGW(SWITCH_TAG, "Cannot read state - parent component not available");
    return;
  }
  
  if (!parent_->is_connection_ready()) {
    ESP_LOGV(SWITCH_TAG, "Cannot read state for '%s' - parent not ready", this->get_name().c_str());
    return;
  }
  
  ESP_LOGV(SWITCH_TAG, "Reading current state for '%s' from register %d", 
           this->get_name().c_str(), register_address_);
  
  parent_->read_register_async(register_address_, [this](uint16_t value) {
    ESP_LOGD(SWITCH_TAG, "Read register %d for '%s': 0x%04X", 
             this->register_address_, this->get_name().c_str(), value);
    this->update_state_from_register_(value);
    
    if (!initial_state_read_) {
      initial_state_read_ = true;
      ESP_LOGD(SWITCH_TAG, "Initial state read completed for '%s'", this->get_name().c_str());
    }
  });
}

void LuxPowerSwitch::update_state_from_register_(uint16_t reg_value) {
  // Check if the bit corresponding to our mask is set
  bool new_state = (reg_value & bitmask_) != 0;
  
  // Log state changes
  if (!initial_state_read_ || this->state != new_state) {
    ESP_LOGI(SWITCH_TAG, "Switch '%s' state %s: %s (register: 0x%04X, mask: 0x%04X)", 
             this->get_name().c_str(),
             initial_state_read_ ? "changed" : "initialized",
             new_state ? "ON" : "OFF",
             reg_value, bitmask_);
  }
  
  this->publish_state(new_state);
}

uint16_t LuxPowerSwitch::prepare_binary_value_(uint16_t old_value, uint16_t mask, bool enable) {
  if (enable) {
    // Set the bit(s) specified by mask
    return old_value | mask;
  } else {
    // Clear the bit(s) specified by mask
    return old_value & ~mask;
  }
}

void LuxPowerSwitch::handle_write_result_(bool success) {
  pending_write_ = false;
  
  if (success) {
    ESP_LOGD(SWITCH_TAG, "Write operation completed successfully for '%s'", this->get_name().c_str());
  } else {
    ESP_LOGE(SWITCH_TAG, "Write operation failed for '%s'", this->get_name().c_str());
  }
}

}  // namespace luxpower_sna
}  // namespace esphome
