#include "switch.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace luxpower_sna {

LuxPowerSwitch::LuxPowerSwitch(LuxpowerSNAComponent *parent, uint16_t register_addr, const std::string &name)
    : parent_(parent), register_address_(register_addr), pending_write_(false), last_write_time_(0) {
  this->set_name(name);
  
  // Auto-register with parent component for centralized updates
  if (parent_) {
    parent_->register_switch(this);
    ESP_LOGD(SWITCH_TAG, "Switch '%s' registered with parent (register %d)", name.c_str(), register_addr);
  } else {
    ESP_LOGE(SWITCH_TAG, "Switch '%s' created without parent component!", name.c_str());
  }
}

void LuxPowerSwitch::setup() {
  ESP_LOGCONFIG(SWITCH_TAG, "Setting up LuxPower Switch '%s'...", this->get_name().c_str());
  
  // Read initial state after 5 seconds (allow parent to establish connection)
  this->set_timeout(5000, [this]() {
    ESP_LOGD(SWITCH_TAG, "Reading initial state for '%s'", this->get_name().c_str());
    this->read_current_state_();
  });
  
  // No periodic polling - parent component will handle centralized updates
}

void LuxPowerSwitch::dump_config() {
  ESP_LOGCONFIG(SWITCH_TAG, "LuxPower Switch:");
  ESP_LOGCONFIG(SWITCH_TAG, "  Name: %s", this->get_name().c_str());
  ESP_LOGCONFIG(SWITCH_TAG, "  Register: %d (0x%04X)", register_address_, register_address_);
  ESP_LOGCONFIG(SWITCH_TAG, "  Parent: %s", parent_ ? "Connected" : "Missing");
}

void LuxPowerSwitch::write_state(bool state) {
  if (parent_ == nullptr) {
    ESP_LOGW(SWITCH_TAG, "Cannot write state - parent component not available");
    return;
  }
  
  if (!parent_->is_connection_ready()) {
    ESP_LOGW(SWITCH_TAG, "Cannot write state for '%s' - parent not ready", this->get_name().c_str());
    return;
  }
  
  if (pending_write_) {
    ESP_LOGW(SWITCH_TAG, "Write already pending for '%s', ignoring new request", this->get_name().c_str());
    return;
  }
  
  ESP_LOGI(SWITCH_TAG, "Writing state for '%s': %s (register %d)", 
           this->get_name().c_str(), state ? "ON" : "OFF", register_address_);
  
  pending_write_ = true;
  last_write_time_ = millis();
  uint16_t value = state ? 1 : 0;
  
  parent_->write_register_async(register_address_, value, [this, state](bool success) {
    this->handle_write_result_(success);
    
    if (success) {
      ESP_LOGI(SWITCH_TAG, "Successfully wrote state for '%s'", this->get_name().c_str());
      // Confirm the write by reading back the state
      this->set_timeout(2000, [this]() {
        this->read_current_state_();
      });
    } else {
      ESP_LOGW(SWITCH_TAG, "Failed to write state for '%s'", this->get_name().c_str());
      // Read current state to restore correct state in UI
      this->set_timeout(1000, [this]() {
        this->read_current_state_();
      });
    }
  });
}

void LuxPowerSwitch::update_state_from_parent() {
  // Only update if we're not in the middle of a write operation
  if (pending_write_) {
    uint32_t time_since_write = millis() - last_write_time_;
    if (time_since_write < 5000) {  // Wait 5 seconds after write before allowing updates
      ESP_LOGV(SWITCH_TAG, "Skipping state update for '%s' - recent write operation", this->get_name().c_str());
      return;
    }
  }
  
  ESP_LOGV(SWITCH_TAG, "Centralized state update for '%s'", this->get_name().c_str());
  this->read_current_state_();
}

void LuxPowerSwitch::read_current_state_() {
  if (parent_ == nullptr) {
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
  });
}

void LuxPowerSwitch::update_state_from_register_(uint16_t value) {
  bool new_state = (value != 0);
  
  // Log state changes
  if (this->state != new_state) {
    ESP_LOGI(SWITCH_TAG, "Switch '%s' state changed: %s -> %s (register value: 0x%04X)", 
             this->get_name().c_str(), 
             this->state ? "ON" : "OFF", 
             new_state ? "ON" : "OFF",
             value);
  }
  
  this->publish_state(new_state);
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
