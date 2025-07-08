#pragma once

#include "esphome/components/sensor/sensor.h"
#include "luxpower_inverter.h" // Include this to get LuxpowerRegType definition

namespace esphome {
namespace luxpower_sna {

class LuxpowerSnaSensor : public sensor::Sensor {
public:
  // Setters for internal variables
  void set_register_address(uint16_t reg_address) { this->register_address_ = reg_address; }
  void set_reg_type(LuxpowerRegType reg_type) { this->reg_type_ = reg_type; }
  void set_bank(uint8_t bank) { this->bank_ = bank; }

  // Getters for internal variables
  uint16_t get_register_address() const { return this->register_address_; }
  LuxpowerRegType get_reg_type() const { return this->reg_type_; }
  uint8_t get_bank() const { return this->bank_; }

protected:
  uint16_t register_address_; // The Modbus register address this sensor reads from
  LuxpowerRegType reg_type_;  // The type of register (e.g., INT, FLOAT_DIV10)
  uint8_t bank_;              // The data bank for the register (if applicable)
};

} // namespace luxpower_sna
} // namespace esphome
