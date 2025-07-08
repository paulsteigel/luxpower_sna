// custom_components/luxpower_inverter/sensor.h
#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

// Forward declaration of the main component
namespace esphome {
namespace luxpower_inverter {
class LuxPowerInverterComponent; // Forward declaration
enum LuxpowerRegType; // Forward declaration of the enum
} // namespace luxpower_inverter
} // namespace esphome

namespace esphome {
namespace luxpower_inverter {

// --- Custom Luxpower Sensor Class ---
class LuxpowerSensor : public sensor::Sensor {
 public:
  void set_parent(LuxPowerInverterComponent *parent) { this->parent_ = parent; }
  void set_register_address(uint16_t addr) { this->register_address_ = addr; }
  void set_reg_type(LuxpowerRegType type) { this->reg_type_ = type; }
  void set_bank(uint8_t bank) { this->bank_ = bank; }

  // Getters for debugging/logging
  uint16_t get_register_address() const { return this->register_address_; }
  uint8_t get_bank() const { return this->bank_; }

  // Method to update this sensor's state from the component's parsed data
  void update_state(uint16_t raw_value);

 protected:
  LuxPowerInverterComponent *parent_; // Pointer to the main component
  uint16_t register_address_;
  LuxpowerRegType reg_type_;
  uint8_t bank_;
};

} // namespace luxpower_inverter
} // namespace esphome