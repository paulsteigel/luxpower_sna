#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "luxpower_sna_constants.h" // Include the constants file for LuxpowerRegType

namespace esphome {
namespace luxpower_sna {

// Forward declaration of LuxPowerInverter is typically needed if LuxpowerSnaSensor
// holds a pointer to LuxPowerInverter (e.g., for communication).
// class LuxPowerInverter;

class LuxpowerSnaSensor : public sensor::Sensor, public Component {
 public:
  // Constructor for the sensor, takes the register address from YAML
  LuxpowerSnaSensor(uint16_t register_address) : register_address_(register_address) {}

  // Setter for the register type from YAML config
  void set_reg_type(LuxpowerRegType reg_type) { this->reg_type_ = reg_type; }

  // Getter for the register type
  LuxpowerRegType get_reg_type() const { return this->reg_type_; }

  // Optional: Setter to link this sensor to its parent inverter component
  // void set_parent(LuxPowerInverter *parent) { this->parent_ = parent; }

  // ESPHome methods
  void dump_config() override; // Required for dumping component configuration
  float get_setup_priority() const override { return setup_priority::DATA; } // Standard priority

 protected:
  uint16_t register_address_; // The Modbus register address this sensor reads from
  LuxpowerRegType reg_type_;  // How to interpret the raw register value

  // Optional: Pointer to the parent LuxPowerInverter component
  // LuxPowerInverter *parent_{nullptr};
};

} // namespace luxpower_sna
} // namespace esphome
