// src/esphome/components/luxpower_sna/luxpower_sna_sensor.h
#pragma once

#include "esphome/components/sensor/sensor.h" // For the base sensor::Sensor class
#include "luxpower_sna_constants.h"          // <--- This should be line 4 or close to it
#include <string>                            // For std::string
#include <cstdint>                           // For uint16_t, uint8_t

// Forward declaration for LuxPowerInverterComponent
// This tells the compiler that LuxPowerInverterComponent is a class,
// without needing its full definition here. This prevents circular includes.
namespace esphome {
namespace luxpower_sna {
class LuxPowerInverterComponent; // This is the forward declaration
} // namespace luxpower_sna
} // namespace esphome

namespace esphome {
namespace luxpower_sna {

class LuxpowerSnaSensor : public sensor::Sensor {
public:
  // Constructor
  LuxpowerSnaSensor(const std::string &name, uint16_t register_address, LuxpowerRegType reg_type, uint8_t bank);

  // ESPHome lifecycle methods (if implemented in luxpower_sna_sensor.cpp)
  void setup() override;
  void dump_config() override;
  void loop() override;

  // Setter for parent component pointer
  void set_parent(LuxPowerInverterComponent *parent) { parent_ = parent; }

  // Getters for sensor properties
  uint16_t get_register_address() const { return register_address_; }
  LuxpowerRegType get_reg_type() const { return reg_type_; }
  uint8_t get_bank() const { return bank_; }

protected:
  LuxPowerInverterComponent *parent_; // Pointer to the main inverter component
  uint16_t register_address_;
  LuxpowerRegtype reg_type_;
  uint8_t bank_;
};

} // namespace luxpower_sna
} // namespace esphome