// src/esphome/components/luxpower_sna/luxpower_sna_sensor.h
#pragma once

#include "esphome/core/component.h" // <--- ADD THIS INCLUDE for Component base class
#include "esphome/components/sensor/sensor.h"
#include "consts.h" // For LuxpowerRegType enum

namespace esphome {
namespace luxpower_sna {

// Forward declaration of the main LuxPowerInverterComponent
class LuxPowerInverterComponent;

class LuxpowerSnaSensor : public sensor::Sensor, public Component { // <--- ADD Component INHERITANCE HERE
public:
  // Setters for the sensor's specific configuration
  void set_parent(LuxPowerInverterComponent *parent) { this->parent_ = parent; }
  // --- ADD THESE SETTERS (and their backing members if not already present) ---
  void set_register_address(uint16_t reg_addr) { this->register_address_ = reg_addr; }
  void set_reg_type(LuxpowerRegType reg_type) { this->reg_type_ = reg_type; }
  void set_bank(uint8_t bank) { this->bank_ = bank; }
  // --- END ADD ---

  // Getters for properties, used by the main component to interpret values
  uint16_t get_register_address() const { return this->register_address_; }
  LuxpowerRegType get_reg_type() const { return this->reg_type_; }
  uint8_t get_bank() const { return this->bank_; }

  // Overrides for Component lifecycle (optional, if this sensor needs its own setup/loop)
  // void setup() override {}
  // void loop() override {}
  // float get_setup_priority() const override { return esphome::setup_priority::DATA; }

protected:
  LuxPowerInverterComponent *parent_;
  uint16_t register_address_; // The Modbus register address this sensor corresponds to
  LuxpowerRegType reg_type_;  // How to interpret the raw register value
  uint8_t bank_;              // Which bank this register belongs to (e.g., 0, 1, 2)
};

} // namespace luxpower_sna
} // namespace esphome
