// src/esphome/components/luxpower_sna/luxpower_sna_sensor.h
// Updated to remove 'override' declarations for setup, dump_config, loop

#pragma once

#include "esphome/components/sensor/sensor.h" // For the base sensor::Sensor class
#include "luxpower_sna_constants.h"          // For LuxpowerRegType enum definition
#include <string>                            // For std::string
#include <cstdint>                           // For uint16_t, uint8_t

// Forward declaration for LuxPowerInverterComponent
namespace esphome {
namespace luxpower_sna {
class LuxPowerInverterComponent;
} // namespace luxpower_sna
} // namespace esphome

namespace esphome {
namespace luxpower_sna {

class LuxpowerSnaSensor : public sensor::Sensor {
public:
  // Constructor
  LuxpowerSnaSensor(const std::string &name, uint16_t register_address, LuxpowerRegType reg_type, uint8_t bank);

  // REMOVED explicit 'override' declarations for setup(), dump_config(), loop() from the .h file.
  // The base class methods still exist and are called by ESPHome's core.
  // Implementations in .cpp for these functions can remain if needed, but they won't be overriding.

  // Setter for parent component pointer
  void set_parent(LuxPowerInverterComponent *parent) { parent_ = parent; }

  // Getters for sensor properties
  uint16_t get_register_address() const { return register_address_; }
  LuxpowerRegType get_reg_type() const { return reg_type_; }
  uint8_t get_bank() const { return bank_; }

protected:
  LuxPowerInverterComponent *parent_; // Pointer to the main inverter component
  uint16_t register_address_;
  LuxpowerRegType reg_type_;
  uint8_t bank_;
};

} // namespace luxpower_sna
} // namespace esphome
